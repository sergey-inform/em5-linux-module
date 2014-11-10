#include "em5.h"

int em5_embus_init(void);
void em5_embus_free(void);
int embus_do(em5_cmd cmd, void* kaddr, size_t sz);
void embus_reset(void);
