#ifndef PORT_DRIVER_H
#define PORT_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

void serial_puts(const char* str);
void serial_init_stage1();
void serial_putc(char c);
#ifdef __cplusplus
}
#endif

#endif // PORT_DRIVER_H