/* Copyright (C) 2023-2026 CascadiaVoxel LLC

    nanoPRC is free software: you can redistribute it and/or modify it under
    the terms of the GNU Affero General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    nanoPRC is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public
    License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with nanoPRC. If not, see <https://www.gnu.org/licenses/>.
*/

/* prc_api_get_version reports the library's version: the string the
   generated prc_version.h holds as PRC_VERSION, with no leading "v". */

#include <stdio.h>
#include <string.h>
#include "prc_test.h"
#include "prc_api.h"
#include "prc_version.h"

int
main(void)
{
    const char *v;

    PRC_TEST_BEGIN("test_version");

    v = prc_api_get_version();
    PRC_ASSERT_NOT_NULL(v);
    printf("  prc_api_get_version: \"%s\", PRC_VERSION: \"%s\"\n", v, PRC_VERSION);
    PRC_ASSERT(strlen(v) > 0);
    PRC_ASSERT(strcmp(v, "unknown") != 0);
    PRC_ASSERT(strcmp(v, PRC_VERSION) == 0);
    PRC_ASSERT(v[0] != 'v');
    /* The same pointer each time: a static string, not an allocation. */
    PRC_ASSERT(prc_api_get_version() == v);

    PRC_TEST_END;
}
