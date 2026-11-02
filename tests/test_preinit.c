int cold_preinit_ping(void);

int main(void) {
    /* Deliberately no cold_code_manager_init()/use(): the stub must abort with
       a clear "no current manager" message.  The CTest script checks it. */
    return cold_preinit_ping();
}
