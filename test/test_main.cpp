// Host test runner: ./pixled_tests [filter]
#include "board_cases.h"
#include "mapping_cases.h"
#include "frames_cases.h"
#include "ddp_cases.h"
#include "dmx_input_cases.h"
#include "controller_cases.h"
#include "tree_cases.h"

int main(int argc, char** argv) { return check::run(argc > 1 ? argv[1] : nullptr) ? 1 : 0; }
