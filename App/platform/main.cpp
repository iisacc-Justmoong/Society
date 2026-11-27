#include <iostream>
#include <string_view>

namespace {

/// Report the current structural entry point without performing product actions.
/// The caller supplies the argument list and both output streams explicitly.
/// Returns zero for supported diagnostics and a nonzero value for all other calls.
int run(int argc, char *const argv[], std::ostream &output, std::ostream &errors)
{
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        output << "platform: --help, --status; application functionality is not implemented.\n";
        return 0;
    }
    if (argc == 2 && std::string_view(argv[1]) == "--status") {
        output << R"({"application":"platform","implemented":false})" << '\n';
        return 0;
    }
    errors << "platform functionality is not implemented.\n";
    return 2;
}

} // namespace

/// Pass process arguments and streams to this application's entry point.
int main(int argc, char *argv[])
{
    return run(argc, argv, std::cout, std::cerr);
}

