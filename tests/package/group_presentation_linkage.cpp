#include <uni/gui/nodes/presentation.h>

int main() {
    using namespace Uni::GUI::Nodes;
    GroupPresentation first;
    GroupPresentation second;
    return first == second && first.SharesStyleWith(second) ? 0 : 1;
}
