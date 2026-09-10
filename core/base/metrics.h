#ifndef REAKTOR_METRICS_H
#define REAKTOR_METRICS_H

float reaktor_scale(void);
void  reaktor_set_scale(float scale);

int   reaktor_px(float logical);


float reaktor_dpi_query_scale(void *native_window);

#endif
