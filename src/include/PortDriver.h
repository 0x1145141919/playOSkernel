#ifndef PORT_DRIVER_H
#define PORT_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

void serial_puts(const char* str);
void serial_init();
#ifdef __cplusplus
}
#endif

#endif // PORT_DRIVER_H