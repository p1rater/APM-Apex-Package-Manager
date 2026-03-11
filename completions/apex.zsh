#compdef apex
# Zsh completion for the apex package manager

_apex_commands() {
    local -a cmds
    cmds=(
        'install:Install packages'
        'remove:Remove packages'
        'upgrade:Upgrade all packages'
        'sync:Synchronise repository indexes'
        'search:Search for packages'
        'info:Show package information'
        'list:List installed packages'
        'owns:Find which package owns a file'
        'check:Verify installed package integrity'
        'orphans:List orphaned packages'
        'autoremove:Remove orphaned packages'
        'clean:Clean the package cache'
        'key:Manage GPG keys'
        'repo:Manage repositories'
        'build:Build a package from APEXBUILD'
    )
    _describe 'command' cmds
}

_apex_installed() {
    local -a pkgs
    pkgs=( ${(f)"$(apex list 2>/dev/null | awk '{print $1}')"} )
    _describe 'installed packages' pkgs
}

_apex_available() {
    local -a pkgs
    pkgs=( ${(f)"$(apex search "$words[CURRENT]" 2>/dev/null | \
        grep '^[a-zA-Z]' | awk '{print $1}' | sed 's|.*/||')"} )
    _describe 'available packages' pkgs
}

_apex() {
    local state

    _arguments \
        '(-h --help)'{-h,--help}'[Show help]' \
        '(-v --version)'{-v,--version}'[Show version]' \
        '--nodeps[Skip dependency checks]' \
        '--force[Force operation]' \
        '--noconfirm[Skip confirmation]' \
        '--asdeps[Mark as dependency]' \
        '--asexplicit[Mark as explicitly installed]' \
        '--recursive[Recursive remove]' \
        '--noscripts[Skip install scripts]' \
        '--nocolor[Disable colour]' \
        '--root=[Set install root]:dir:_files -/' \
        '1: :_apex_commands' \
        '*: :->args'

    case $state in
    args)
        case $words[2] in
        install)        _apex_available ;;
        remove|check|info) _apex_installed ;;
        owns)           _files ;;
        key)            _values 'subcommand' add del list ;;
        repo)           _values 'subcommand' add remove enable disable list sync ;;
        esac
        ;;
    esac
}

_apex "$@"
