#if defined(_WIN32)
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
int foundation_plugin_fixture_without_query(void) {
    return 0;
}
