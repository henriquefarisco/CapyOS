int run_update_agent_tests(void);
int run_update_transact_tests(void);
int run_audit_events_tests(void);
int main(void) {
    int failures = run_update_agent_tests();
    failures += run_audit_events_tests();
    failures += run_update_transact_tests();
    return failures != 0;
}
