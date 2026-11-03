int cold_preinit_ping(void);

int main(void) {
    /* Deliberately no manager: the slow path must trap. */
    return cold_preinit_ping();
}
