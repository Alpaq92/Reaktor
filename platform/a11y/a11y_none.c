#include "a11y.h"

void
reaktor_a11y_platform_init(reaktor_a11y_action activate,
                           reaktor_a11y_action focus, void *user)
{
    (void)activate; (void)focus; (void)user;
}

void
reaktor_a11y_platform_push(const reaktor_a11y *a, unsigned focus_id)
{
    (void)a; (void)focus_id;
}

void
reaktor_a11y_platform_drain(void)
{
}
