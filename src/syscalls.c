#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _JNK_
#include <sys/stat.h>
enum {
 UART_FR_RXFE = 0x10,
 UART_FR_TXFF = 0x20,
 UART0_ADDR = 0x10009000,
};
 
#define UART_DR(baseaddr) (*(unsigned int *)(baseaddr))
#define UART_FR(baseaddr) (*(((unsigned int *)(baseaddr))+6))
 
int _close(int file) { return -1; }
 
int _fstat(int file, struct stat *st) {
 st->st_mode = S_IFCHR;
 return 0;
}
 
int _isatty(int file) { return 1; }
 
int _lseek(int file, int ptr, int dir) { return 0; }
 
int _open(const char *name, int flags, int mode) { return -1; }
 
int _read(int file, char *ptr, int len) {
 int todo;
 if(len == 0)
  return 0;
 while(UART_FR(UART0_ADDR) & UART_FR_RXFE);
 *ptr++ = UART_DR(UART0_ADDR);
 for(todo = 1; todo < len; todo++) {
  if(UART_FR(UART0_ADDR) & UART_FR_RXFE) {
   break;
 }
 *ptr++ = UART_DR(UART0_ADDR);
 }
 return todo;
}
 
char *heap_end = 0;
caddr_t _sbrk(int incr) {
 
  return (caddr_t)0;
 }
 
 
int _write(int file, char *ptr, int len) {
 #ifdef JNK
 int todo;
 
 for (todo = 0; todo < len; todo++) {
  UART_DR(UART0_ADDR) = *ptr++;
 }
 return len;
 #endif
 }
#endif
void _exit(int status)
{
    (void)status;

    while (1) {
        __asm volatile ("wfi");
    }
}

 void _kill(void)
 {

 }

 void _getpid(void)
 {

 }
