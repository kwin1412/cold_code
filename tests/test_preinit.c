int cold_preinit_ping(void);

int main(void) {
    /* Deliberately no cold_code_init(): the stub must abort with a clear
       "cold_code_init() was not called" message. The CTest script checks it. */
    return cold_preinit_ping();
}
