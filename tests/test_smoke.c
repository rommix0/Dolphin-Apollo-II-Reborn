#include "test.h"

void suite_smoke(void)
{
    CHECK_EQ(1 + 1, 2);
    CHECK_STR("ok", "ok");
}
