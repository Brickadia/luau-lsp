#include "doctest.h"
#include "Fixture.h"
#include "LSP/WorkspaceFileResolver.hpp"
#include "Luau/Ast.h"
#include "Luau/FileResolver.h"
#include "LuauFileUtils.hpp"

TEST_SUITE_BEGIN("WorkspaceFileResolverTests");

TEST_CASE_FIXTURE(Fixture, "resolve_alias_does_nothing_if_string_doesnt_start_with_@_symbol")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "test": "test.lua"
        }
    }
    )");

    CHECK_EQ(resolveAlias("test", workspace.fileResolver.defaultConfig, {}), std::nullopt);
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_handles_variations_with_directory_separator")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "test": "folder"
        }
    }
    )");

    CHECK_EQ(resolveAlias("@test", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("folder"));
    CHECK_EQ(resolveAlias("@test/", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("folder"));
    CHECK_EQ(resolveAlias("@test/foo", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("folder/foo"));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_handles_if_alias_was_defined_with_trailing_slash")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "test": "folder/"
        }
    }
    )");

    auto x = resolveAlias("@test", workspace.fileResolver.defaultConfig, {});

    CHECK_EQ(resolveAlias("@test", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("folder"));
    CHECK_EQ(resolveAlias("@test/", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("folder"));
    CHECK_EQ(resolveAlias("@test/foo", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("folder/foo"));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_supports_absolute_paths")
{
#ifdef _WIN32
    auto basePath = "C:/Users/test/folder";
#else
    auto basePath = "/home/folder";
#endif

    std::string source = R"(
        {
            "aliases": {
                "test": "{basePath}"
            }
        }
    )";
    replace(source, "{basePath}", basePath);
    loadLuaurc(source);

    CHECK_EQ(resolveAlias("@test", workspace.fileResolver.defaultConfig, {}), Uri::file(basePath));
    CHECK_EQ(resolveAlias("@test/", workspace.fileResolver.defaultConfig, {}), Uri::file(basePath));
    CHECK_EQ(resolveAlias("@test/foo", workspace.fileResolver.defaultConfig, {}), Uri::file(basePath).resolvePath("foo"));
}

#ifdef _WIN32
TEST_CASE_FIXTURE(Fixture, "resolve_alias_normalizes_drive_letter_case_on_windows")
{
    // Alias values with uppercase drive letters must resolve to the same module name as
    // files opened directly (which always have lowercase drive letters via Uri::file()).
    // Mismatch causes the type checker to treat the same file as two distinct modules.
    loadLuaurc(R"(
    {
        "aliases": {
            "test": "C:/Users/test/folder"
        }
    }
    )");

    auto resolved = resolveAlias("@test/module", workspace.fileResolver.defaultConfig, {});
    REQUIRE(resolved.has_value());
    // fsPath() must match Uri::file() which lowercases the drive letter
    CHECK_EQ(resolved->fsPath(), Uri::file("C:/Users/test/folder/module").fsPath());
}
#endif

TEST_CASE_FIXTURE(Fixture, "resolve_alias_supports_tilde_expansion")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "test": "~/definitions"
        }
    }
    )");

    auto home = getHomeDirectory();
    REQUIRE(home);

    CHECK_EQ(resolveAlias("@test", workspace.fileResolver.defaultConfig, {}), Uri::file(Luau::FileUtils::joinPaths(*home, "definitions")));
    CHECK_EQ(resolveAlias("@test/", workspace.fileResolver.defaultConfig, {}), Uri::file(Luau::FileUtils::joinPaths(*home, "definitions")));
    CHECK_EQ(resolveAlias("@test/foo", workspace.fileResolver.defaultConfig, {}), Uri::file(Luau::FileUtils::joinPaths(*home, "definitions/foo")));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_supports_self_alias")
{
    auto basePath = workspace.fileResolver.rootUri;

    CHECK_EQ(resolveAlias("@self", workspace.fileResolver.defaultConfig, basePath), basePath);
    CHECK_EQ(resolveAlias("@self/foo", workspace.fileResolver.defaultConfig, basePath), basePath.resolvePath("foo"));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_supports_chained_aliases")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "std": "lute/std/libs",
            "lint": "@std/commands/lint/types",
            "transform": "@std/commands/transform/types"
        }
    }
    )");

    auto stdBase = workspace.fileResolver.rootUri.resolvePath("lute/std/libs");

    CHECK_EQ(resolveAlias("@lint", workspace.fileResolver.defaultConfig, {}), stdBase.resolvePath("commands/lint/types"));
    CHECK_EQ(resolveAlias("@lint/foo", workspace.fileResolver.defaultConfig, {}), stdBase.resolvePath("commands/lint/types/foo"));
    CHECK_EQ(resolveAlias("@transform", workspace.fileResolver.defaultConfig, {}), stdBase.resolvePath("commands/transform/types"));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_resolves_chained_alias_with_intermediate_path")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "a": "@b/sub",
            "b": "libs"
        }
    }
    )");

    CHECK_EQ(resolveAlias("@a", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("libs/sub"));
    CHECK_EQ(resolveAlias("@a/file", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("libs/sub/file"));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_resolves_longer_alias_chain")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "a": "@b",
            "b": "@c",
            "c": "deep"
        }
    }
    )");

    CHECK_EQ(resolveAlias("@a", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("deep"));
    CHECK_EQ(resolveAlias("@a/file", workspace.fileResolver.defaultConfig, {}), workspace.fileResolver.rootUri.resolvePath("deep/file"));
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_returns_nullopt_on_cyclic_aliases")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "a": "@b",
            "b": "@c",
            "c": "@a"
        }
    }
    )");

    CHECK_EQ(resolveAlias("@a", workspace.fileResolver.defaultConfig, {}), std::nullopt);
}

TEST_CASE_FIXTURE(Fixture, "resolve_alias_returns_nullopt_on_longer_alias_cycle")
{
    loadLuaurc(R"(
    {
        "aliases": {
            "a": "@b",
            "b": "@c",
            "c": "@a"
        }
    }
    )");

    CHECK_EQ(resolveAlias("@a", workspace.fileResolver.defaultConfig, {}), std::nullopt);
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_chained_alias_end_to_end")
{
    auto mainPath = tempDir.touch_child("main.luau");
    tempDir.touch_child("packages/utils/init.luau");
    tempDir.write_child(".luaurc", R"({
        "aliases": {
            "utils": "@libs/utils",
            "libs": "./packages"
        }
    })");

    Luau::ModuleInfo baseContext{mainPath};
    auto result = workspace.platform->resolveStringRequire(&baseContext, "@utils", workspace.limits);

    REQUIRE(result.has_value());
    auto packagesUri = workspace.fileResolver.rootUri.resolvePath("packages");
    CHECK(packagesUri.isAncestorOf(Uri::file(result->name)));
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_tilde_alias_end_to_end")
{
    // This test goes through resolveStringRequire (not resolveAlias directly) to catch
    // regressions where tilde expansion is broken in the require resolution path.
    // The .luaurc is written to disk because require resolution reads configs from disk.
    auto mainPath = tempDir.touch_child("main.luau");
    tempDir.write_child(".luaurc", R"({
        "aliases": {
            "test": "~/definitions"
        }
    })");

    auto home = getHomeDirectory();
    REQUIRE(home);

    Luau::ModuleInfo baseContext{mainPath};
    auto result = workspace.platform->resolveStringRequire(&baseContext, "@test/module", workspace.limits);

    REQUIRE(result.has_value());
    // The ~ should be expanded: the resolved path must be under the home directory
    auto definitionsUri = Uri::file(Luau::FileUtils::joinPaths(*home, "definitions"));
    CHECK(definitionsUri.isAncestorOf(Uri::file(result->name)));
}

TEST_CASE_FIXTURE(Fixture, "string require doesn't add file extension if already exists")
{
    Luau::ModuleInfo baseContext{workspace.fileResolver.getModuleName(workspace.rootUri)};
    auto resolved = workspace.platform->resolveStringRequire(&baseContext, "Module.luau", workspace.limits);

    REQUIRE(resolved.has_value());
#ifdef _WIN32
    CHECK(endsWith(resolved->name, "\\Module.luau"));
#else
    CHECK(endsWith(resolved->name, "/Module.luau"));
#endif
}

TEST_CASE_FIXTURE(Fixture, "string require doesn't replace a non-luau/lua extension")
{
    Luau::ModuleInfo baseContext{workspace.fileResolver.getModuleName(workspace.rootUri)};
    auto resolved = workspace.platform->resolveStringRequire(&baseContext, "Module.mod", workspace.limits);

    REQUIRE(resolved.has_value());
#ifdef _WIN32
    CHECK(endsWith(resolved->name, "\\Module.mod.lua"));
#else
    CHECK(endsWith(resolved->name, "/Module.mod.lua"));
#endif
}

TEST_CASE("string_require_resolves_relative_to_file_integration_test")
{
    // This integration test needs access to real test data files, so we create a workspace
    // rooted in the actual project directory rather than using Fixture's temp directory
    TestClient client;
    client.globalConfig = Luau::LanguageServer::defaultTestClientConfiguration();
    client.definitionsFiles.emplace("@test", "./tests/testdata/standard_definitions.d.luau");

    auto cwd = Luau::FileUtils::getCurrentWorkingDirectory();
    REQUIRE(cwd);

    WorkspaceFolder workspace(&client, "$TEST_WORKSPACE", Uri::file(*cwd), std::nullopt);
    workspace.fileResolver.defaultConfig.mode = Luau::Mode::Strict;
    workspace.setupWithConfiguration(client.globalConfig);
    workspace.isReady = true;

    auto moduleName = workspace.fileResolver.getModuleName(workspace.rootUri.resolvePath("tests/testdata/requires/relative_to_file/main.luau"));
    auto result = workspace.frontend.check(moduleName);

    REQUIRE_EQ(result.errors.size(), 0);

    auto module = workspace.frontend.moduleResolver.getModule(moduleName);
    REQUIRE(module);
    REQUIRE(module->hasModuleScope());

    auto binding = module->getModuleScope()->linearSearchForBinding("other");
    REQUIRE(binding);
    CHECK_EQ(Luau::toString(Luau::follow(binding->typeId)), "number");
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_relative_to_file")
{
    auto projectLibMainPath = tempDir.touch_child("project/lib/main.luau");
    auto projectLibUtilsPath = tempDir.touch_child("project/lib/utils.luau");
    auto projectOtherPath = tempDir.touch_child("project/other.luau");

    Luau::ModuleInfo baseContext{projectLibMainPath};
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./utils", workspace.limits)->name),
        Uri::file(projectLibUtilsPath));
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "../other", workspace.limits)->name),
        Uri::file(projectOtherPath));
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_a_directory_as_the_init_luau_file")
{
    auto projectLibMainPath = tempDir.touch_child("project/lib/main.luau");
    auto projectOtherPath = tempDir.touch_child("project/other/init.luau");

    Luau::ModuleInfo baseContext{projectLibMainPath};
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "../other", workspace.limits)->name),
        Uri::file(projectOtherPath));
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_relative_to_directory_for_init_luau")
{
    auto toolsInitPath = tempDir.touch_child("tools/init.luau");
    auto toolsFilePath = tempDir.touch_child("tools/file.luau");
    auto projectSiblingPath = tempDir.touch_child("project/sibling.luau");
    auto projectDirectoryUtilsPath = tempDir.touch_child("project/directory/utils.luau");
    auto projectDirectoryInitPath = tempDir.touch_child("project/directory/init.luau");

    Luau::ModuleInfo baseContext{projectDirectoryInitPath};
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./sibling", workspace.limits)->name),
        Uri::file(projectSiblingPath));

    CHECK_EQ(
        Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "../tools", workspace.limits)->name), Uri::file(toolsInitPath));
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "../tools/file", workspace.limits)->name),
        Uri::file(toolsFilePath));

    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./utils", workspace.limits)->name),
        Uri::file(tempDir.path() + "/project/utils.lua"));
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./directory/utils", workspace.limits)->name),
        Uri::file(projectDirectoryUtilsPath));
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolve_file_named_luau")
{
    auto mainPath = tempDir.touch_child("project/main.luau");
    auto luauPath = tempDir.touch_child("project/luau.luau");

    Luau::ModuleInfo baseContext{mainPath};
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./luau", workspace.limits)->name), Uri::file(luauPath));
}

TEST_CASE("is_init_luau_file")
{
    CHECK_EQ(isInitLuauFile(Uri::file("foo/init.lua")), true);
    CHECK_EQ(isInitLuauFile(Uri::file("foo/init.luau")), true);
    CHECK_EQ(isInitLuauFile(Uri::file("foo/init.client.luau")), true);
    CHECK_EQ(isInitLuauFile(Uri::file("foo/init.server.luau")), true);

    CHECK_EQ(isInitLuauFile(Uri::file("foo/utils.luau")), false);
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_self_alias")
{
    auto projectInitPath = tempDir.touch_child("project/init.luau");
    auto projectUtilsPath = tempDir.touch_child("project/utils.luau");

    Luau::ModuleInfo baseContext{projectInitPath};
    CHECK_EQ(Uri::file(workspace.fileResolver.platform->resolveStringRequire(&baseContext, "@self/utils", workspace.limits)->name),
        Uri::file(projectUtilsPath));
}

TEST_CASE_FIXTURE(Fixture, "init_luau_files_should_not_be_aware_of_sibling_luaurc_files")
{
    auto initPath = tempDir.touch_child("project/code/init.luau");
    auto siblingFilePath = tempDir.touch_child("project/code/sibling.luau");
    tempDir.write_child("project/code/.luaurc", R"({
        "aliases": {
            "test": "test"
        }
    })");

    auto initConfig = workspace.fileResolver.getConfig(workspace.fileResolver.getModuleName(Uri::file(initPath)), workspace.limits);
    auto siblingConfig = workspace.fileResolver.getConfig(workspace.fileResolver.getModuleName(Uri::file(siblingFilePath)), workspace.limits);

    CHECK_EQ(initConfig.aliases.size(), 0);
    CHECK_EQ(siblingConfig.aliases.size(), 1);
    CHECK(siblingConfig.aliases.find("test"));
}

TEST_CASE_FIXTURE(Fixture, "init_luau_files_are_aware_of_luaurc_files_that_are_sibling_to_its_parent_directory")
{
    auto initPath = tempDir.touch_child("project/code/init.luau");
    tempDir.write_child("project/.luaurc", R"({
        "aliases": {
            "test": "test"
        }
    })");

    auto initConfig = workspace.fileResolver.getConfig(workspace.fileResolver.getModuleName(Uri::file(initPath)), workspace.limits);

    CHECK_EQ(initConfig.aliases.size(), 1);
    CHECK(initConfig.aliases.find("test"));
}

TEST_CASE_FIXTURE(Fixture, "support_config_luau")
{
    auto fooPath = tempDir.touch_child("project/code/foo.luau");
    tempDir.write_child("project/.config.luau", R"(
        return {
            luau = {
                aliases = {
                    test = "test"
                }
            }
        }
    )");

    auto fooConfig = workspace.fileResolver.getConfig(workspace.fileResolver.getModuleName(Uri::file(fooPath)), workspace.limits);

    CHECK_EQ(fooConfig.aliases.size(), 1);
    CHECK(fooConfig.aliases.find("test"));
}

#ifndef _WIN32
TEST_CASE_FIXTURE(Fixture, "string_require_resolves_symlinked_file")
{
    auto mainPath = tempDir.touch_child("project/main.luau");
    auto targetPath = tempDir.touch_child("project/real_module.luau");

    // Create a symlink: project/linked_module.luau -> project/real_module.luau
    std::filesystem::create_symlink(targetPath, tempDir.path() + "/project/linked_module.luau");

    Luau::ModuleInfo baseContext{mainPath};
    auto resolved = workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./linked_module", workspace.limits);

    REQUIRE(resolved.has_value());
    CHECK(endsWith(resolved->name, "/project/linked_module.luau"));
}

TEST_CASE_FIXTURE(Fixture, "string_require_resolves_symlinked_directory")
{
    auto mainPath = tempDir.touch_child("project/main.luau");
    tempDir.touch_child("real_lib/init.luau");

    // Create a symlink: project/lib -> real_lib
    std::filesystem::create_symlink(tempDir.path() + "/real_lib", tempDir.path() + "/project/lib");

    Luau::ModuleInfo baseContext{mainPath};
    auto resolved = workspace.fileResolver.platform->resolveStringRequire(&baseContext, "./lib", workspace.limits);

    REQUIRE(resolved.has_value());
    CHECK(endsWith(resolved->name, "/project/lib/init.luau"));
}
#endif

TEST_SUITE_END();
