#include "doctest.h"
#include "Fixture.h"
#include "LSP/IostreamHelpers.hpp"
#include "Platform/StringRequireAutoImporter.hpp"

static std::optional<lsp::CompletionItem> getItem(const std::vector<lsp::CompletionItem>& items, const std::string& label)
{
    for (const auto& item : items)
        if (item.label == label)
            return item;

    return std::nullopt;
}

static std::vector<lsp::CompletionItem> filterAutoImports(const std::vector<lsp::CompletionItem>& items, const std::string& moduleName = "")
{
    std::vector<lsp::CompletionItem> results;

    for (const auto& item : items)
    {
        if (item.kind == lsp::CompletionItemKind::Module)
            if (moduleName.empty() || item.label == moduleName)
                results.emplace_back(item);
    }

    std::sort(results.begin(), results.end(),
        [](const auto& a, const auto& b)
        {
            return a.detail < b.detail;
        });

    return results;
}

TEST_SUITE_BEGIN("AutoImports");

TEST_CASE("make_valid_variable_names")
{
    using namespace Luau::LanguageServer::AutoImports;
    CHECK_EQ(makeValidVariableName("react spring"), "react_spring");
    CHECK_EQ(makeValidVariableName("react-spring"), "react_spring");
    CHECK_EQ(makeValidVariableName("react@spring"), "react_spring");
}

TEST_CASE_FIXTURE(Fixture, "services_do_not_show_up_in_autocomplete_if_imports_is_disabled")
{
    client->globalConfig.completion.imports.enabled = false;
    auto [source, marker] = sourceWithMarker(R"(
        |
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    CHECK_EQ(getItem(result, "ReplicatedStorage"), std::nullopt);
}

using namespace Luau::LanguageServer::AutoImports;

static StringRequireAutoImporterContext createContext(Fixture* fixture, const Uri& uri, FindImportsVisitor* importsVisitor)
{
    auto moduleName = fixture->workspace.fileResolver.getModuleName(uri);
    auto sourceModule = fixture->workspace.frontend.getSourceModule(moduleName);
    auto textDocument = fixture->workspace.fileResolver.getTextDocument(uri);

    // Need to pass the visitor in so that it isn't destroyed after function ends
    importsVisitor->visit(sourceModule->root);

    StringRequireAutoImporterContext ctx{
        moduleName,
        Luau::NotNull(textDocument),
        defaultModuleVisitor(fixture->workspace.frontend),
        Luau::NotNull(&fixture->workspace),
        Luau::NotNull(&fixture->client->globalConfig.completion.imports),
        0,
        Luau::NotNull(importsVisitor),
    };

    return ctx;
}

TEST_CASE_FIXTURE(Fixture, "module_name_processed_for_label")
{
    CHECK_EQ(requireNameFromModuleName("foo.luau"), "foo");
    CHECK_EQ(requireNameFromModuleName("bar.lua"), "bar");
    CHECK_EQ(requireNameFromModuleName("a/b/c/baz.luau"), "baz");
}

TEST_CASE_FIXTURE(Fixture, "string_require_label_is_file_name")
{
    newDocument("foo.luau", "");
    newDocument("bar.lua", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", "");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 2);

    auto firstItem = getItem(items, "foo");
    REQUIRE(firstItem);
    CHECK_EQ(firstItem->label, "foo");
    CHECK_EQ(firstItem->additionalTextEdits.size(), 1);
    CHECK_EQ(firstItem->additionalTextEdits[0].newText, "local foo = require(\"./foo\")\n");

    auto secondItem = getItem(items, "bar");
    CHECK_EQ(secondItem->label, "bar");
    CHECK_EQ(secondItem->additionalTextEdits.size(), 1);
    CHECK_EQ(secondItem->additionalTextEdits[0].newText, "local bar = require(\"./bar\")\n");
    CHECK(getItem(items, "foo"));
}

TEST_CASE_FIXTURE(Fixture, "string_requires_does_not_include_source_module")
{
    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", "");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 0);
}

TEST_CASE_FIXTURE(Fixture, "string_require_does_not_include_modules_matching_globs")
{
    newDocument("library.luau", "");
    newDocument("library.spec.luau", "");
    newDocument("Packages/React.luau", "");
    newDocument("Packages/_Index/ReactLib.luau", "");

    client->globalConfig.completion.imports.ignoreGlobs = {"*.spec.luau", "**/_Index/**"};

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", "");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 2);
    CHECK(getItem(items, "library"));
    CHECK(getItem(items, "React"));

    CHECK_FALSE(getItem(items, "library.spec"));
    CHECK_FALSE(getItem(items, "ReactLib"));
}

TEST_CASE_FIXTURE(Fixture, "string_require_does_not_include_modules_that_are_already_required")
{
    newDocument("library.luau", "");
    newDocument("Packages/React.luau", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", R"(
        local React = require("./Packages/React")
    )");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 1);
    CHECK(getItem(items, "library"));
    CHECK_FALSE(getItem(items, "React"));
}

TEST_CASE_FIXTURE(Fixture, "string_require_stops_suggesting_a_module_once_its_file_is_deleted")
{
    auto library = newDocument("library.luau", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", "");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);
    REQUIRE(getItem(items, "library"));

    // Simulate the file being renamed / deleted on disk
    lsp::FileEvent event{library, lsp::FileChangeType::Deleted};
    workspace.onDidChangeWatchedFiles({event});

    items.clear();
    suggestStringRequires(ctx, items);
    CHECK_FALSE(getItem(items, "library"));
}

TEST_CASE_FIXTURE(Fixture, "string_require_uses_aliases")
{
    AliasMap aliases{};
    aliases["Packages"] = {Uri::file("Packages").fsPath(), "", "Packages"};
    auto style = ImportRequireStyle::Auto;

    auto from = Uri::file("project/user.luau");
    CHECK_EQ(computeRequirePath(from, Uri::file("Packages/React.luau"), aliases, style).first, "@Packages/React");
}

TEST_CASE_FIXTURE(Fixture, "dont_use_aliases_when_always_relative_specified")
{
    AliasMap aliases{};
    aliases["Packages"] = {Uri::file("Packages").fsPath(), "", "Packages"};
    auto style = ImportRequireStyle::AlwaysRelative;

    auto from = Uri::file("project/user.luau");
    CHECK_EQ(computeRequirePath(from, Uri::file("Packages/React.luau"), aliases, style).first, "../Packages/React");
}

TEST_CASE_FIXTURE(Fixture, "always_use_possible_aliases_when_always_absolute_specified")
{
    AliasMap aliases{};
    aliases["project"] = {Uri::file("project").fsPath(), "", "project"};
    auto style = ImportRequireStyle::AlwaysAbsolute;

    auto from = Uri::file("project/user.luau");
    CHECK_EQ(computeRequirePath(from, Uri::file("project/sibling.luau"), aliases, style).first, "@project/sibling");
    CHECK_EQ(computeRequirePath(from, Uri::file("project/directory/child.luau"), aliases, style).first, "@project/directory/child");
    CHECK_EQ(computeRequirePath(from, Uri::file("other/lib.luau"), aliases, style).first, "../other/lib");
}

TEST_CASE_FIXTURE(Fixture, "string_require_compute_best_alias")
{
    AliasMap aliases{};
    aliases["project"] = {Uri::file("project").fsPath(), "", "Project"};
    aliases["packages"] = {Uri::file("packages").fsPath(), "", "Packages"};
    aliases["nestedproject"] = {Uri::file("project/nested").fsPath(), "", "NestedProject"};

    CHECK_EQ(computeBestAliasedPath(Uri::file("project/user.luau"), aliases), "@Project/user");
    CHECK_EQ(computeBestAliasedPath(Uri::file("packages/React.luau"), aliases), "@Packages/React");
    CHECK_EQ(computeBestAliasedPath(Uri::file("packages/nested/library/React.luau"), aliases), "@Packages/nested/library/React");
    CHECK_EQ(computeBestAliasedPath(Uri::file("project/nested/lib.luau"), aliases), "@NestedProject/lib");
    CHECK_EQ(computeBestAliasedPath(Uri::file("random/test.luau"), aliases), std::nullopt);
}

TEST_CASE_FIXTURE(Fixture, "string_require_uses_best_alias_from_luaurc")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "ReplicatedStorage": "src/shared",
            "Modules": "src/shared/Modules",
        }
    })");

    client->globalConfig.completion.imports.enabled = true;


    newDocument("src/shared/Modules/Module.luau", "return {}");

    auto [source, marker] = sourceWithMarker(R"(
        |
    )");

    auto uri = newDocument("src/client/file.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    auto imports = filterAutoImports(result);

    REQUIRE_EQ(imports.size(), 1);
    REQUIRE_EQ(imports[0].additionalTextEdits.size(), 1);
    CHECK_EQ(imports[0].additionalTextEdits[0].newText, "local Module = require(\"@Modules/Module\")\n");
}

TEST_CASE_FIXTURE(Fixture, "string_require_includes_aliased_files_from_external_directory")
{
    TempDir library("include_aliases_external_lib");
    auto module = library.write_child("module.luau", R"(
        return {}
    )");

    auto luaurc = std::string(R"(
    {
        "aliases": {
            "library": "{filepath}"
        }
    }
    )");
    replace(luaurc, "{filepath}", library.path());
    loadLuaurc(luaurc);
    auto moduleName = workspace.fileResolver.getModuleName(Uri::file(module));

    client->globalConfig.index.enabled = true;

    client->globalConfig.completion.imports.enabled = true;
    workspace.setupWithConfiguration(client->globalConfig);

    auto [source, marker] = sourceWithMarker(R"(
        |
    )");

    auto uri = newDocument("src/client/file.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    auto imports = filterAutoImports(result);

    REQUIRE_EQ(imports.size(), 1);
    REQUIRE_EQ(imports[0].additionalTextEdits.size(), 1);
    CHECK_EQ(imports[0].additionalTextEdits[0].newText, "local module = require(\"@library/module\")\n");
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_correctly_for_init_luau_file")
{
    AliasMap aliases{};
    auto style = ImportRequireStyle::Auto;

    auto from = Uri::file("project/code/init.luau");
    CHECK_EQ(computeRequirePath(from, Uri::file("project/code/sibling.luau"), aliases, style).first, "@self/sibling");
    CHECK_EQ(computeRequirePath(from, Uri::file("project/file.luau"), aliases, style).first, "./file");
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_to_directory_that_contains_init_luau_file")
{
    AliasMap aliases{};
    auto style = ImportRequireStyle::Auto;

    auto from = Uri::file("project/file.luau");
    auto to = Uri::file("project/code/init.luau");

    CHECK_EQ(requireNameFromModuleName(workspace.fileResolver.getModuleName(to)), "code");
    CHECK_EQ(computeRequirePath(from, to, aliases, style).first, "./code");
}

TEST_CASE_FIXTURE(Fixture, "string_require_inserts_at_top_of_file")
{
    client->globalConfig.completion.imports.enabled = true;


    newDocument("library.luau", "");

    auto [source, marker] = sourceWithMarker(R"(

        |
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    auto imports = filterAutoImports(result);

    REQUIRE_EQ(imports.size(), 1);
    REQUIRE_EQ(imports[0].additionalTextEdits.size(), 1);
    CHECK_EQ(imports[0].additionalTextEdits[0].range.start.line, 0);
}

TEST_CASE_FIXTURE(Fixture, "string_require_inserts_after_hot_comments")
{
    client->globalConfig.completion.imports.enabled = true;


    newDocument("library.luau", "");

    auto [source, marker] = sourceWithMarker(R"(
        --!strict

        |
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    auto imports = filterAutoImports(result);

    REQUIRE_EQ(imports.size(), 1);
    REQUIRE_EQ(imports[0].additionalTextEdits.size(), 1);
    CHECK_EQ(imports[0].additionalTextEdits[0].range.start.line, 2);
}

TEST_CASE_FIXTURE(Fixture, "string_require_inserts_after_hot_comments_2")
{
    client->globalConfig.completion.imports.enabled = true;


    newDocument("library.luau", "");

    auto [source, marker] = sourceWithMarker(R"(
        --!strict
        --!optimize 2

        |
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    auto imports = filterAutoImports(result);

    REQUIRE_EQ(imports.size(), 1);
    REQUIRE_EQ(imports[0].additionalTextEdits.size(), 1);
    CHECK_EQ(imports[0].additionalTextEdits[0].range.start.line, 3);
}

TEST_CASE_FIXTURE(Fixture, "string_requires_are_inserted_lexicographically_1")
{
    newDocument("library.luau", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", R"(
        local zebra = require("./zebra")
    )");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 1);
    auto item = getItem(items, "library");
    REQUIRE(item);
    REQUIRE_EQ(item->additionalTextEdits.size(), 1);
    CHECK_EQ(item->additionalTextEdits[0].range.start.line, 1);
}

TEST_CASE_FIXTURE(Fixture, "string_requires_are_inserted_lexicographically_2")
{
    newDocument("library.luau", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", R"(
        local alphabet = require("./alphabet")
    )");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 1);
    auto item = getItem(items, "library");
    REQUIRE(item);
    REQUIRE_EQ(item->additionalTextEdits.size(), 1);
    CHECK_EQ(item->additionalTextEdits[0].range.start.line, 2);
}

TEST_CASE_FIXTURE(Fixture, "string_requires_are_inserted_lexicographically_3")
{
    newDocument("library.luau", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", R"(
        local alphabet = require("./alphabet")
        local book = require("./book")
        local zebra = require("./zebra")
    )");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 1);
    auto item = getItem(items, "library");
    REQUIRE(item);
    REQUIRE_EQ(item->additionalTextEdits.size(), 1);
    CHECK_EQ(item->additionalTextEdits[0].range.start.line, 3);
}

TEST_CASE_FIXTURE(Fixture, "relative_requires_are_inserted_after_aliases")
{
    auto astRoot = parse(R"(
        local React = require("@Packages/React")
    )");

    FindImportsVisitor importsVisitor;
    importsVisitor.visit(astRoot);

    auto insertedLineNumber = computeBestLineForRequire(importsVisitor, TextDocument({}, {}, {}, {}), "./alphabet", 0);

    CHECK_EQ(insertedLineNumber, 2);
}

TEST_CASE_FIXTURE(Fixture, "aliased_requires_are_inserted_before_relative_requires")
{
    auto astRoot = parse(R"(
        local alphabet = require("./alphabet")
    )");

    FindImportsVisitor importsVisitor;
    importsVisitor.visit(astRoot);

    auto insertedLineNumber = computeBestLineForRequire(importsVisitor, TextDocument({}, {}, {}, {}), "@Packages/React", 0);

    CHECK_EQ(insertedLineNumber, 0);
}

TEST_CASE_FIXTURE(Fixture, "auto_import_empty_require_statement")
{
    client->globalConfig.completion.imports.enabled = true;

    newDocument("alphabet.luau", "");

    auto [source, marker] = sourceWithMarker(R"(
        local Value = require()

        |
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    auto imports = filterAutoImports(result);

    REQUIRE_EQ(imports.size(), 1);
    auto item = getItem(imports, "alphabet");
    REQUIRE(item);
    REQUIRE_EQ(item->additionalTextEdits.size(), 1);
    CHECK_EQ(item->additionalTextEdits[0].range.start.line, 1);
}

TEST_CASE_FIXTURE(Fixture, "auto_imports_do_not_show_when_completion_property")
{
    client->globalConfig.completion.imports.enabled = true;
    auto [source, marker] = sourceWithMarker(R"(
        local x = foo.ba|
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    CHECK_FALSE(getItem(result, "ReplicatedStorage"));
}

TEST_CASE_FIXTURE(Fixture, "auto_imports_do_not_show_when_indexing_variable_inside_table")
{
    client->globalConfig.completion.imports.enabled = true;
    auto [source, marker] = sourceWithMarker(R"(
        local foo = {}
        local x = {
            foo.|
        }
    )");

    auto uri = newDocument("foo.luau", source);

    lsp::CompletionParams params;
    params.textDocument = lsp::TextDocumentIdentifier{uri};
    params.position = marker;

    auto result = workspace.completion(params, nullptr);
    CHECK_FALSE(getItem(result, "ReplicatedStorage"));
}

TEST_CASE_FIXTURE(Fixture, "string_require_uses_const_when_configured")
{
    newDocument("foo.luau", "");

    client->globalConfig.completion.imports.useConst = true;

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", "");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    REQUIRE_EQ(items.size(), 1);
    auto fooItem = getItem(items, "foo");
    REQUIRE(fooItem);
    CHECK_EQ(fooItem->additionalTextEdits[0].newText, "const foo = require(\"./foo\")\n");
}

TEST_CASE_FIXTURE(Fixture, "const_require_is_not_duplicated_when_already_imported")
{
    newDocument("library.luau", "");

    FindImportsVisitor visitor;
    auto user = newDocument("user.luau", R"(
        const library = require("./library")
    )");
    auto ctx = createContext(this, user, &visitor);

    std::vector<lsp::CompletionItem> items;
    suggestStringRequires(ctx, items);

    CHECK_EQ(items.size(), 0);
}


TEST_SUITE_END();
