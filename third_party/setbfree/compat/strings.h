/* Neptune : <strings.h> n'existe pas sous MSVC ; setBfree n'y utilise que ces deux fonctions. Ce dossier n'est ajouté aux
 * chemins d'inclusion que pour MSVC (core/CMakeLists.txt). */
#pragma once
#include <string.h>
#define strcasecmp _stricmp
#define strncasecmp _strnicmp
