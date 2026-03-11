# /usr/share/bash-completion/completions/apex
# Bash completion for the apex package manager

_apex_packages_installed() {
    apex list 2>/dev/null | awk '{print $1}'
}

_apex_packages_available() {
    apex search "$1" 2>/dev/null | grep '^[a-zA-Z]' | awk '{print $1}' | sed 's|.*/||'
}

_apex_complete() {
    local cur prev words cword
    _init_completion || return

    local commands="install remove upgrade sync search info list owns check orphans autoremove clean key repo build"

    case $cword in
    1)
        COMPREPLY=( $(compgen -W "$commands --help --version" -- "$cur") )
        return
        ;;
    esac

    local cmd="${words[1]}"

    case "$cmd" in
    install)
        COMPREPLY=( $(compgen -W "$(_apex_packages_available "$cur")" -- "$cur") )
        ;;
    remove|check|info)
        COMPREPLY=( $(compgen -W "$(_apex_packages_installed)" -- "$cur") )
        ;;
    owns)
        _filedir
        ;;
    key)
        COMPREPLY=( $(compgen -W "add del list" -- "$cur") )
        ;;
    repo)
        COMPREPLY=( $(compgen -W "add remove enable disable list sync" -- "$cur") )
        ;;
    *)
        ;;
    esac
}

complete -F _apex_complete apex
