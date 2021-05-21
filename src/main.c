#include "common.h"
#include "parser.h"
#include "vm.h"
#include <gc/gc.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <unistd.h>

extern void dumpError(VM* vm);

static const char* runKindLabels[] = {
    NULL, "file", "module", "eval"
};

static void runFallbackRepl(VM* vm);

int main(int argc, char** argv) {
    const char* run = NULL;
    int runKind = 0; // 0 - fallback repl, 1 - file, 2 - module, 3 - eval, 4 repl
    bool useFallback = false;
    bool isFreestanding = false;
    bool fullTrace = false;

    int option;
    while ((option = getopt(argc, argv, "e:m:M:fFht")) != -1) {
        switch (option) {
            // e -- Evaluate
            case 'e': {
                if (runKind != 0) {
                    printf("%s: specify only one of -m or -e\n", argv[0]);
                    exit(1);
                }
                run = GC_strdup(optarg);
                runKind = 3;
            } break;
            // m -- import and run Module
            case 'm': {
                if (runKind != 0) {
                    printf("%s: specify only one of -m or -e\n", argv[0]);
                    exit(1);
                }
                run = GC_strdup(optarg);
                runKind = 2;
            } break;
            // M -- add Module path
            case 'M': {
                printf("MODPATH %s\n", optarg);
            } break;
            // f -- use Fallback repl
            case 'f': {
                useFallback = true;
            } break;
            // F -- Freestanding
            case 'F': {
                isFreestanding = true;
            } break;
            case 't': {
                fullTrace = true;
            } break;
            // x -- skip first line of source, allowing hashbangs
            // case 'x': {
            //     // todo: implement
            //     printf("error: -x not implemented\n");
            //     exit(1);
            // } break;
            // h -- show help
            case 'h': {
                printf("usage: %s [options] [file] [--] [args...]\n", argv[0]);
                printf("    -e code    evaluate a line of code\n");
                printf("    -m module  run a module\n");
                printf("    -M path    add a path to search for modules on\n");
                printf("    -f         use fallback repl\n");
                printf("    -F         Freestanding (dont import dragon)\n");
                printf("    -t         don't hide internal Traces\n");
                // printf("    -x\n");
                printf("    -h         show this help message\n");
                exit(0);
            } break;
            case '?': {
                exit(1);
            } break;
            default: {
                assert(0 && "unexpected return code from getopt");
            }
        }
    }
    if (optind < argc && runKind == 0) {
        run = GC_strdup(argv[optind]);
        runKind = 1;
    }

    if (runKind == 0) {
        printf("fruitpunch 3.0\n");
    }

    if (useFallback && runKind != 0) {
        printf("warning: ignoring -f option\n");
    }
    if (runKind == 0 && !useFallback && !isFreestanding) {
        runKind = 4;
    }

    // switch (runKind) {
    //     case 0: printf("REPL\n"); break;
    //     case 1: printf("FILe %s\n", run); break;
    //     case 2: printf("MODULE %s\n", run); break;
    //     case 3: printf("EVAL %s\n", run); break;
    // }

    // todo: pass remaining args into program

    rl_bind_key('\t', rl_insert);
    
    GC_INIT();
    VM vm = { .fullTrace = fullTrace };
    VM_startup(&vm);

    if (!isFreestanding) {
        if (!Module_import(&vm, "dragon", false)) {
            dumpError(&vm);
            return 1;
        }
        vm.root = GET_CONTEXT(Stack_pop(vm.stack));
    }

    switch (runKind) {
        case 0: {
            runFallbackRepl(&vm);
        } break;
        case 1: {
            // todo: do we add containing folder as module?
            // (probably not, module importing should instead be
            //  relative to module specifying import?)
            if (!Module_fromFile(&vm, run, true)) {
                dumpError(&vm);
                return 1;
            }
        } break;
        case 2: {
            if (!Module_import(&vm, run, true)) {
                dumpError(&vm);
                return 1;
            }
        } break;
        case 3: {
            ModuleInfo* module = GC_MALLOC(sizeof(ModuleInfo));
            *module = (ModuleInfo) { "<eval>", run };
            Block* b = fpParse(module);
            if (!b) {
                dumpError(&vm);
                return 1;
            }

            vm.context = Context_create(vm.root);
            if (!VM_eval(&vm, b)) {
                dumpError(&vm);
                return 1;
            } else {
                VM_dump(&vm);
            }
            vm.context = NULL;
        } break;
        case 4: {
            if (!Module_import(&vm, "repl", true)) {
                dumpError(&vm);
                vm.stack->next = 0;
                printf("entering fallback repl...\n");
                runFallbackRepl(&vm);
            }
        } break;
    }

    return 0;
}

static void runFallbackRepl(VM* vm) {
    vm->context = Context_create(vm->root);
    while (1) {
        char* line = readline("#> ");
        if (!line) {
            printf("\n");
            break;
        }
        if (line[0]) add_history(line);
        char* buf = GC_strdup(line);
        free(line);
        // printf("> ");
        // char buf[1024];
        // fgets(buf, 1024, stdin);
        if (buf[0] == '!') {
            if (buf[1] == 'q') break;
            else if (buf[1] == 'd') Context_dump(vm->root);
            else if (buf[1] == 'm') Module_dump(vm);
            else assert(0 && "invalid command");
            continue;
        }

        ModuleInfo* module = GC_MALLOC(sizeof(ModuleInfo));
        *module = (ModuleInfo) { "<repl>", GC_STRDUP(buf) };
        Block* b = fpParse(module);
        if (!b) continue;
        // fpDumpBlock(b);

        if (!VM_eval(vm, b)) {
            // printf("%s:1: \033[31;7m%s\033[0m %s\n",
            //     si.name,
            //     Symbol_repr(vm.exSymbol),
            //     vm.exMessage);
            dumpError(vm);
        } else {
            VM_dump(vm);
        }
        vm->stack->next = 0;
    }
    vm->context = NULL;
}
