#pragma once
#include <vector>
#include "nlohmann/json.hpp"

struct ClientDiagnosticsConfiguration
{
    /// Whether to also compute diagnostics for dependents when a file changes
    bool includeDependents = true;
    /// Whether to compute diagnostics for a whole workspace
    bool workspace = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientDiagnosticsConfiguration, includeDependents, workspace)

struct ClientTypesConfiguration
{
    /// Any definition files to load globally
    std::unordered_map<std::string, std::string> definitionFiles{};
    /// A list of globals to remove from the global scope. Accepts full libraries or particular functions (e.g., `table` or `table.clone`)
    std::vector<std::string> disabledGlobals{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientTypesConfiguration, definitionFiles, disabledGlobals)

enum struct InlayHintsParameterNamesConfig
{
    None,
    Literals,
    All
};
NLOHMANN_JSON_SERIALIZE_ENUM(InlayHintsParameterNamesConfig, {
                                                                 {InlayHintsParameterNamesConfig::None, "none"},
                                                                 {InlayHintsParameterNamesConfig::Literals, "literals"},
                                                                 {InlayHintsParameterNamesConfig::All, "all"},
                                                             })

struct ClientInlayHintsConfiguration
{
    InlayHintsParameterNamesConfig parameterNames = InlayHintsParameterNamesConfig::None;
    bool variableTypes = false;
    bool parameterTypes = false;
    bool functionReturnTypes = false;
    bool hideHintsForErrorTypes = false;
    bool hideHintsForMatchingParameterNames = true;
    size_t typeHintMaxLength = 50;
    /// Whether type inlay hints should be made insertable
    bool makeInsertable = true;

    inline bool operator==(const ClientInlayHintsConfiguration& rhs) const
    {
        return this->parameterNames == rhs.parameterNames && this->variableTypes == rhs.variableTypes && this->parameterTypes == rhs.parameterTypes &&
               this->functionReturnTypes == rhs.functionReturnTypes && this->hideHintsForErrorTypes == rhs.hideHintsForErrorTypes &&
               this->hideHintsForMatchingParameterNames == rhs.hideHintsForMatchingParameterNames && this->typeHintMaxLength == rhs.typeHintMaxLength;
    }

    inline bool operator!=(const ClientInlayHintsConfiguration& rhs) const
    {
        return !(*this == rhs);
    }
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientInlayHintsConfiguration, parameterNames, variableTypes, parameterTypes, functionReturnTypes,
    hideHintsForErrorTypes, hideHintsForMatchingParameterNames, typeHintMaxLength, makeInsertable);

struct ClientHoverConfiguration
{
    bool enabled = true;
    bool showTableKinds = false;
    bool multilineFunctionDefinitions = false;
    bool includeStringLength = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientHoverConfiguration, enabled, showTableKinds, multilineFunctionDefinitions, includeStringLength);

enum struct ImportRequireStyle
{
    Auto,
    AlwaysRelative,
    AlwaysAbsolute,
};
NLOHMANN_JSON_SERIALIZE_ENUM(ImportRequireStyle, {
                                                     {ImportRequireStyle::Auto, "auto"},
                                                     {ImportRequireStyle::AlwaysRelative, "alwaysRelative"},
                                                     {ImportRequireStyle::AlwaysAbsolute, "alwaysAbsolute"},
                                                 })

struct ClientCompletionImportsConfiguration
{
    /// Whether we should suggest automatic imports in completions
    bool enabled = true;
    /// Whether requires should be suggested in auto-import
    bool suggestRequires = true;
    /// The style of the auto-imported require
    ImportRequireStyle requireStyle = ImportRequireStyle::Auto;
    /// Whether groups of imports should be separated by an empty line
    bool separateGroupsWithLine = false;
    /// Files that match these globs will not be shown during auto-import
    std::vector<std::string> ignoreGlobs{};
    /// Whether to use `const` instead of `local` for auto-imported declarations
    bool useConst = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(
    ClientCompletionImportsConfiguration, enabled, suggestRequires, requireStyle, separateGroupsWithLine, ignoreGlobs, useConst);

struct ClientCompletionAnonymousAutofillConfiguration
{
    /// Whether to show the "function (anonymous autofilled)" generated function completion entry
    bool enabled = true;
    /// Whether to include type annotations in the generated function snippet
    bool addTypeAnnotations = true;
    /// Whether to add snippet tabstops on each parameter name for quick editing
    bool addTabstopForParameters = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientCompletionAnonymousAutofillConfiguration, enabled, addTypeAnnotations, addTabstopForParameters);

struct ClientCompletionConfiguration
{
    bool enabled = true;
    /// Whether to automatically autocomplete end
    bool autocompleteEnd = false;
    /// Whether we should suggest automatic imports in completions
    /// DEPRECATED: USE `completion.imports.enabled` INSTEAD
    bool suggestImports = false;
    /// Automatic imports configuration
    ClientCompletionImportsConfiguration imports{};
    /// Automatically add parentheses to a function call
    bool addParentheses = true;
    /// If parentheses are added, include a $0 tabstop after the parentheses
    bool addTabstopAfterParentheses = true;
    /// If parentheses are added, fill call arguments with parameter names
    bool fillCallArguments = true;
    /// Whether to show non-function properties when performing a method call with a colon
    bool showPropertiesOnMethodCall = false;
    /// Whether to show keywords (`if` / `then` / `and` / etc.) during autocomplete
    bool showKeywords = true;
    /// Whether to show the "function (anonymous autofilled)" generated function entry
    /// DEPRECATED: USE `completion.anonymousAutofilledFunction.enabled` INSTEAD
    bool showAnonymousAutofilledFunction = true;
    /// Configuration for the anonymous autofilled function completion entry
    ClientCompletionAnonymousAutofillConfiguration anonymousAutofilledFunction{};
    /// Whether to show deprecated items in autocomplete suggestions
    bool showDeprecatedItems = true;
    /// Enables the fragment autocomplete system for performance improvements
    bool enableFragmentAutocomplete = true;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientCompletionConfiguration, enabled, autocompleteEnd, suggestImports, imports, addParentheses,
    addTabstopAfterParentheses, fillCallArguments, showPropertiesOnMethodCall, showKeywords, showAnonymousAutofilledFunction, anonymousAutofilledFunction,
    showDeprecatedItems, enableFragmentAutocomplete);

struct ClientSignatureHelpConfiguration
{
    bool enabled = true;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientSignatureHelpConfiguration, enabled);

struct ClientRequireConfiguration
{
    // DEPRECATED: USE NEW REQUIRE-BY-STRING SEMANTICS INSTEAD.
    bool useOriginalRequireByStringSemantics = false;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientRequireConfiguration, useOriginalRequireByStringSemantics);

struct ClientIndexConfiguration
{
    /// Whether the whole workspace should be indexed. If disabled, only limited support is
    // available for features such as "Find All References" and "Rename"
    bool enabled = true;
    /// The maximum amount of files that can be indexed
    size_t maxFiles = 10000;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientIndexConfiguration, enabled, maxFiles);

struct ClientFFlagsConfiguration
{
    /// Enable all (boolean) Luau FFlags by default. These flags can later be overriden by `#luau-lsp.fflags.override#`
    bool enableByDefault = true;
    // Override FFlags passed to Luau
    std::unordered_map<std::string, std::string> override;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientFFlagsConfiguration, enableByDefault, override);

struct ClientBytecodeConfiguration
{
    int debugLevel = 1;
    int typeInfoLevel = 1;
    std::string vectorLib;
    std::string vectorCtor;
    std::string vectorType;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientBytecodeConfiguration, debugLevel, typeInfoLevel, vectorLib, vectorCtor, vectorType)

struct ClientFormatConfiguration
{
    /// Whether to convert single/double quotes to backticks when typing `{` inside strings
    bool convertQuotes = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientFormatConfiguration, convertQuotes);

struct ClientPluginFileSystemConfiguration
{
    /// Whether filesystem access is enabled for plugins
    bool enabled = false;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientPluginFileSystemConfiguration, enabled)

struct ClientPluginConfiguration
{
    /// Whether plugins are enabled
    bool enabled = false;
    /// Paths to plugin Luau scripts
    std::vector<std::string> paths{};
    /// Timeout for plugin execution in milliseconds
    size_t timeoutMs = 5000;
    /// Configuration for plugin filesystem access
    ClientPluginFileSystemConfiguration fileSystem{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientPluginConfiguration, enabled, paths, timeoutMs, fileSystem)

enum struct LSPPlatformConfig
{
    Standard,
};
NLOHMANN_JSON_SERIALIZE_ENUM(LSPPlatformConfig, {
                                                    {LSPPlatformConfig::Standard, "standard"},
                                                })

struct ClientPlatformConfiguration
{
    LSPPlatformConfig type = LSPPlatformConfig::Standard;
};

NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientPlatformConfiguration, type);

// These are the passed configuration options by the client, prefixed with `luau-lsp.`
// Here we also define the default settings
struct ClientConfiguration
{
    /// Whether to automatically autocomplete end
    /// DEPRECATED: Use completion.autocompleteEnd instead
    bool autocompleteEnd = false;
    std::vector<std::string> ignoreGlobs{};
    ClientPlatformConfiguration platform{};
    ClientDiagnosticsConfiguration diagnostics{};
    ClientTypesConfiguration types{};
    ClientInlayHintsConfiguration inlayHints{};
    ClientHoverConfiguration hover{};
    ClientCompletionConfiguration completion{};
    ClientSignatureHelpConfiguration signatureHelp{};
    ClientRequireConfiguration require{};
    ClientIndexConfiguration index{};
    ClientFFlagsConfiguration fflags{};
    ClientBytecodeConfiguration bytecode{};
    ClientFormatConfiguration format{};
    ClientPluginConfiguration plugins{};
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE_WITH_DEFAULT(ClientConfiguration, autocompleteEnd, ignoreGlobs, platform, diagnostics, types,
    inlayHints, hover, completion, signatureHelp, require, index, fflags, bytecode, format, plugins);
