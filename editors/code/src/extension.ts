import * as vscode from "vscode";
import * as os from "os";
import {
  CloseAction,
  CloseHandlerResult,
  ErrorAction,
  ErrorHandler,
  ErrorHandlerResult,
  Executable,
  LanguageClient,
  LanguageClientOptions,
  Message,
  ServerOptions,
  TransportKind,
} from "vscode-languageclient/node";

import {
  registerComputeBytecode,
  registerComputeCodeGen,
  registerComputeCompilerRemarks,
} from "./bytecode";

import { onTypeFormattingMiddleware } from "./onTypeFormattingMiddleware";

import { registerViewInternalSource } from "./internalSource";

import * as utils from "./utils";

let client: LanguageClient | undefined = undefined;
const clientDisposables: vscode.Disposable[] = [];

const FFLAG_KINDS = ["FFlag", "FInt", "DFFlag", "DFInt"];

// Commands the server may ask the client to run through `$/command`.
const SERVER_COMMANDS = new Set(["cursorMove"]);

type FFlags = Record<string, string>;

const isAlphanumericUnderscore = (str: string) => {
  return /^[a-zA-Z0-9_]+$/.test(str);
};

class ClientErrorHandler implements ErrorHandler {
  private readonly restarts: number[];

  constructor(private maxRestartCount: number) {
    this.restarts = [];
  }

  public error(
    _error: Error,
    _message: Message,
    count: number,
  ): ErrorHandlerResult {
    if (count && count <= 3) {
      return { action: ErrorAction.Continue };
    }
    return { action: ErrorAction.Shutdown };
  }

  public closed(): CloseHandlerResult {
    this.restarts.push(Date.now());
    if (this.restarts.length <= this.maxRestartCount) {
      return { action: CloseAction.Restart };
    } else {
      const diff = this.restarts[this.restarts.length - 1] - this.restarts[0];
      if (diff <= 3 * 60 * 1000) {
        return {
          action: CloseAction.DoNotRestart,
          message: `The Luau Language server crashed ${this.maxRestartCount + 1} times in the last 3 minutes. The server will not be restarted. See the output for more information.`,
        };
      } else {
        this.restarts.shift();
        return { action: CloseAction.Restart };
      }
    }
  }
}

const resolveWorkspacePath = (configuredPath: string) => {
  const resolved = utils.resolvePath(configuredPath);
  if (vscode.workspace.workspaceFolders) {
    return utils.resolveUri(vscode.workspace.workspaceFolders[0].uri, resolved);
  }
  return vscode.Uri.file(resolved);
};

const startLanguageServer = async (context: vscode.ExtensionContext) => {
  for (const disposable of clientDisposables) {
    disposable.dispose();
  }
  clientDisposables.splice(0, clientDisposables.length); // empty the list
  if (client) {
    await client.stop();
  }

  console.log("Starting Luau Language Server");

  const args = ["lsp"];
  const debugArgs = ["lsp"];

  const addArg = (argument: string, mode: "All" | "Prod" | "Debug" = "All") => {
    if (mode === "All" || mode === "Prod") {
      args.push(argument);
    }
    if (mode === "All" || mode === "Debug") {
      debugArgs.push(argument);
    }
  };

  const typesConfig = vscode.workspace.getConfiguration("luau-lsp.types");

  // Load extra type definitions
  const definitionFilesConfig =
    typesConfig.get<{ [packageName: string]: string }>("definitionFiles") ??
    {};

  for (const [packageName, definitionPath] of Object.entries(
    definitionFilesConfig,
  )) {
    const uri = resolveWorkspacePath(definitionPath);
    if (await utils.exists(uri)) {
      addArg(`--definitions:${packageName}=${uri.fsPath}`);
    } else {
      vscode.window.showWarningMessage(
        `Definitions file '${packageName}' at ${definitionPath} does not exist, types will not be provided from this file`,
      );
    }
  }

  // Load extra documentation files
  for (const documentationPath of typesConfig.get<string[]>(
    "documentationFiles",
  ) ?? []) {
    const uri = resolveWorkspacePath(documentationPath);
    if (await utils.exists(uri)) {
      addArg(`--docs=${uri.fsPath}`);
    } else {
      vscode.window.showWarningMessage(
        `Documentations file at ${documentationPath} does not exist`,
      );
    }
  }

  // Handle FFlags
  const fflags: FFlags = {};
  const fflagsConfig = vscode.workspace.getConfiguration("luau-lsp.fflags");

  if (!fflagsConfig.get<boolean>("enableByDefault")) {
    addArg("--no-flags-enabled");
  }

  // Handle overrides
  const overridenFFlags = fflagsConfig.get<FFlags>("override");
  if (overridenFFlags) {
    for (let [name, value] of Object.entries(overridenFFlags)) {
      if (!isAlphanumericUnderscore(name)) {
        vscode.window.showWarningMessage(
          `Invalid FFlag name: '${name}'. It can only contain alphanumeric characters`,
        );
      }

      name = name.trim();
      value = value.trim();

      // Strip kind prefix if it was included
      for (const kind of FFLAG_KINDS) {
        if (name.startsWith(`${kind}`)) {
          name = name.substring(kind.length);
        }
      }

      // Validate that the name and value is valid
      if (name.length > 0 && value.length > 0) {
        fflags[name] = value;
      }
    }
  }

  const serverConfiguration =
    vscode.workspace.getConfiguration("luau-lsp.server");

  const serverBinConfig = utils.resolvePath(
    serverConfiguration.get("path", "").trim(),
  );
  const serverBinUri =
    vscode.workspace.workspaceFolders &&
    vscode.workspace.workspaceFolders.length > 0
      ? utils.resolveUri(
          vscode.workspace.workspaceFolders[0].uri,
          serverBinConfig,
        )
      : vscode.Uri.file(serverBinConfig);
  let serverBinPath;

  if (serverBinConfig !== "" && (await utils.exists(serverBinUri))) {
    serverBinPath = serverBinUri.fsPath;
  } else {
    if (serverBinConfig !== "") {
      vscode.window.showWarningMessage(
        `Server binary at path \`${serverBinUri.fsPath}\` does not exist, falling back to bundled binary`,
      );
    }
    serverBinPath = vscode.Uri.joinPath(
      context.extensionUri,
      "bin",
      os.platform() === "win32" ? "server.exe" : "server",
    ).fsPath;
  }

  const transport =
    serverConfiguration.get<"stdio" | "pipe">(
      "communicationChannel",
      "stdio",
    ) === "pipe"
      ? TransportKind.pipe
      : TransportKind.stdio;

  const delayStartup = serverConfiguration.get<boolean>("delayStartup", false);
  if (delayStartup) {
    addArg("--delay-startup");
  }

  // Handle base luaurc
  const baseLuaurcConfig = serverConfiguration.get<string>("baseLuaurc");
  if (baseLuaurcConfig) {
    const uri = resolveWorkspacePath(baseLuaurcConfig);
    if (await utils.exists(uri)) {
      addArg(`--base-luaurc=${uri.fsPath}`);
    } else {
      vscode.window
        .showWarningMessage(
          `Base .luaurc file at ${baseLuaurcConfig} does not exist`,
          "Configure Settings",
        )
        .then((action) => {
          if (action === "Configure Settings") {
            vscode.commands.executeCommand(
              "workbench.action.openSettings",
              "luau-lsp.server.baseLuaurc",
            );
          }
        });
    }
  }

  const run: Executable = {
    command: serverBinPath,
    args,
    transport,
  };

  // If debugging, run the locally build extension, with local type definitions file
  const debug: Executable = {
    command: process.env["LUAU_LSP_SERVER_PATH"]
      ? vscode.Uri.file(process.env["LUAU_LSP_SERVER_PATH"]).fsPath
      : serverBinPath,
    args: debugArgs,
    transport,
  };

  const serverOptions: ServerOptions = { run, debug };

  const clientOptions: LanguageClientOptions = {
    documentSelector: vscode.workspace
      .getConfiguration("luau-lsp")
      .get<boolean>("analyzeLuaFiles", true)
      ? [
          { language: "luau", scheme: "file" },
          { language: "luau", scheme: "untitled" },
          { language: "lua", scheme: "file" },
          { language: "lua", scheme: "untitled" },
        ]
      : [
          { language: "luau", scheme: "file" },
          { language: "luau", scheme: "untitled" },
        ],
    diagnosticPullOptions: {
      onChange: vscode.workspace
        .getConfiguration("luau-lsp.diagnostics")
        .get("pullOnChange", true),
      onSave: vscode.workspace
        .getConfiguration("luau-lsp.diagnostics")
        .get("pullOnSave", true),
    },
    initializationOptions: {
      fflags,
    },
    markdown: {
      supportHtml: true,
    },
    errorHandler: new ClientErrorHandler(4),
    middleware: {
      provideOnTypeFormattingEdits: onTypeFormattingMiddleware,
    },
  };

  client = new LanguageClient(
    "luau",
    "Luau Language Server",
    serverOptions,
    clientOptions,
  );

  // Register commands
  client.onNotification("$/command", (params) => {
    if (SERVER_COMMANDS.has(params.command)) {
      vscode.commands.executeCommand(params.command, params.data);
    }
  });

  clientDisposables.push(
    vscode.commands.registerCommand(
      "luau-lsp.rename",
      async (
        uriString: string,
        position: { line: number; character: number },
      ) => {
        const uri = vscode.Uri.parse(uriString);
        const pos = new vscode.Position(position.line, position.character);
        const editor = vscode.window.activeTextEditor;
        if (editor && editor.document.uri.toString() === uri.toString()) {
          editor.selection = new vscode.Selection(pos, pos);
          await vscode.commands.executeCommand("editor.action.rename");
        }
      },
    ),
  );

  clientDisposables.push(...registerComputeBytecode(context, client));
  clientDisposables.push(...registerComputeCompilerRemarks(context, client));
  clientDisposables.push(...registerComputeCodeGen(context, client));
  clientDisposables.push(...registerViewInternalSource(context, client));

  console.log("LSP Setup");
  await client.start();
};

export async function activate(context: vscode.ExtensionContext) {
  console.log("Luau LSP activated");

  context.subscriptions.push(
    vscode.commands.registerCommand("luau-lsp.reloadServer", async () => {
      vscode.window.showInformationMessage("Reloading Language Server");
      await startLanguageServer(context);
    }),
    vscode.commands.registerCommand("luau-lsp.flushTimeTrace", async () => {
      if (client) {
        client.sendNotification("$/flushTimeTrace");
      }
    }),
  );

  context.subscriptions.push(
    vscode.workspace.onDidChangeConfiguration((e) => {
      if (e.affectsConfiguration("luau-lsp.server")) {
        vscode.window
          .showInformationMessage(
            "Luau LSP server configuration has changed, reload server for this to take effect.",
            "Reload Language Server",
          )
          .then((command) => {
            if (command === "Reload Language Server") {
              vscode.commands.executeCommand("luau-lsp.reloadServer");
            }
          });
      } else if (
        e.affectsConfiguration("luau-lsp.fflags") ||
        e.affectsConfiguration("luau-lsp.completion.enableFragmentAutocomplete")
      ) {
        vscode.window
          .showInformationMessage(
            "Luau FFlags have been changed, reload server for this to take effect.",
            "Reload Language Server",
          )
          .then((command) => {
            if (command === "Reload Language Server") {
              vscode.commands.executeCommand("luau-lsp.reloadServer");
            }
          });
      } else if (
        e.affectsConfiguration("luau-lsp.types") ||
        e.affectsConfiguration("luau-lsp.platform.type") ||
        e.affectsConfiguration("luau-lsp.analyzeLuaFiles")
      ) {
        vscode.window
          .showInformationMessage(
            "Luau type definitions have been changed, reload server for this to take effect.",
            "Reload Language Server",
          )
          .then((command) => {
            if (command === "Reload Language Server") {
              vscode.commands.executeCommand("luau-lsp.reloadServer");
            }
          });
      }
    }),
  );

  await startLanguageServer(context);
}

export async function deactivate() {
  return Promise.allSettled([
    client?.stop(),
    clientDisposables.map((disposable) => disposable.dispose()),
  ]);
}
