#include "neptune/core/FileUtil.h"

#include <fstream>
#include <system_error>

namespace neptune::core::fileutil
{
bool writeAtomic (const std::filesystem::path& path, const std::string& text, std::string* error)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.has_parent_path())
    {
        fs::create_directories (path.parent_path(), ec);
        if (ec)
        {
            if (error) *error = "cannot create directory: " + ec.message();
            return false;
        }
    }

    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out (tmp, std::ios::binary | std::ios::trunc);
        if (! out)
        {
            if (error) *error = "cannot open for writing: " + tmp.string();
            return false;
        }
        out.write (text.data(), static_cast<std::streamsize> (text.size()));
        out.flush();
        if (! out)
        {
            if (error) *error = "write failed: " + tmp.string();
            fs::remove (tmp, ec);
            return false;
        }
    }

    fs::rename (tmp, path, ec); // remplace l'existant (POSIX et Windows)
    if (ec)
    {
        if (error) *error = "rename failed: " + ec.message();
        fs::remove (tmp, ec);
        return false;
    }
    return true;
}

bool readAll (const std::filesystem::path& path, std::string& out)
{
    std::ifstream in (path, std::ios::binary);
    if (! in) return false;
    out.assign (std::istreambuf_iterator<char> (in), std::istreambuf_iterator<char>());
    return ! in.bad();
}
} // namespace neptune::core::fileutil
