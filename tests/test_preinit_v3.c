int cold_preinit_ping(void);

int main(void) {
    /* Deliberately no manager: the stub must abort with "no current manager". */
    return cold_preinit_ping();
}
