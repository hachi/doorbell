#ifndef DOORBELL_H
#define DOORBELL_H

#ifdef __cplusplus
extern "C" {
#endif

void rgb(int, int, int);
void mono(int);
IPAddress broadcastIP();
void ring();

#ifdef  __cplusplus
}
#endif

#endif
