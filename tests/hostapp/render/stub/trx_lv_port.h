#pragma once
bool trx_lv_open(void);
int trx_lv_handler(void);
void trx_lv_buffers(uint16_t **a, uint16_t **b);
bool trx_lv_show_picture(const uint16_t *fb);
void trx_lv_set_place(int x, int y, int w, int h);
void trx_lv_set_canvas(int w, int h);
