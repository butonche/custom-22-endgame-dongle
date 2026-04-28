#include <stdbool.h>
#include <zmk/keymap.h>
#include <zmk/sixdof_mode.h>

#define LAYER_6DOF 5

bool sixdof_is_active(void)
{
    return zmk_keymap_layer_active(LAYER_6DOF);
}
