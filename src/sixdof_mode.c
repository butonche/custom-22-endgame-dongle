#include <stdbool.h>
#include <zmk/sixdof_mode.h>

static bool g_sixdof_active = false;

void sixdof_set_active(bool active)
{
    g_sixdof_active = active;
}

bool sixdof_is_active(void)
{
    return g_sixdof_active;
}
