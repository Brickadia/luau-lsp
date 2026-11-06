#include "LSP/Utils.hpp"
#include "Luau/StringUtils.h"
#include <algorithm>

#include "Luau/TimeTrace.h"
#include "LuauFileUtils.hpp"

std::optional<std::string> getParentPath(const std::string& path)
{
    if (path == "" || path == "." || path == "/")
        return std::nullopt;

    std::string::size_type slash = path.find_last_of("\\/", path.size() - 1);

    if (slash == 0)
        return "/";

    if (slash != std::string::npos)
        return path.substr(0, slash);

    return "";
}

std::string codeBlock(const std::string& language, const std::string& code)
{
    return "```" + language + "\n" + code + "\n" + "```";
}

std::optional<std::string> getHomeDirectory()
{
    if (const char* home = getenv("HOME"))
    {
        return home;
    }
    else if (const char* userProfile = getenv("USERPROFILE"))
    {
        return userProfile;
    }
    else
    {
        return std::nullopt;
    }
}

// Resolves a filesystem path, including any tilde expansion
std::string resolvePath(const std::string& path)
{
    if (Luau::startsWith(path, "~/") || Luau::startsWith(path, "~\\"))
    {
        if (auto home = getHomeDirectory())
            return Luau::FileUtils::joinPaths(*home, path.substr(2));
        else
            // TODO: should we error / return an optional here instead?
            return path;
    }
    else
        return path;
}

void trim_start(std::string& str)
{
    str.erase(0, str.find_first_not_of(" \n\r\t"));
}

std::string removePrefix(const std::string& str, const std::string& prefix)
{
    if (Luau::startsWith(str, prefix))
        return str.substr(prefix.length());
    return str;
}

void trim_end(std::string& str)
{
    str.erase(str.find_last_not_of(" \n\r\t") + 1);
}

void trim(std::string& str)
{
    trim_start(str);
    trim_end(str);
}

std::string toLower(std::string str)
{
    std::transform(str.begin(), str.end(), str.begin(),
        [](unsigned char c)
        {
            return std::tolower(c);
        });
    return str;
}

std::string_view getFirstLine(const std::string_view& str)
{
    size_t eol_char = str.find('\n');
    if (eol_char == std::string::npos)
        return str;
    return str.substr(0, eol_char);
}

bool endsWith(const std::string_view& str, const std::string_view& suffix)
{
    return str.size() >= suffix.size() && 0 == str.compare(str.size() - suffix.size(), suffix.size(), suffix);
}

std::string removeSuffix(const std::string& str, const std::string_view& suffix)
{
    if (endsWith(str, suffix))
        return str.substr(0, str.length() - suffix.size());
    return str;
}

bool replace(std::string& str, const std::string& from, const std::string& to)
{
    size_t start_pos = str.find(from);
    if (start_pos == std::string::npos)
        return false;
    str.replace(start_pos, from.length(), to);
    return true;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to)
{
    if (from.empty())
        return;
    size_t start_pos = 0;
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from.length(), to);
        start_pos += to.length();
    }
}
