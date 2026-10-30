#!/usr/bin/env bash
# Sets the terminal prompt to show your SRN instead of user@hostname.
#
#   This terminal only : source scripts/set_srn_prompt.sh PES2UG24EC000
#   Every new terminal : source scripts/set_srn_prompt.sh PES2UG24EC000 --persist
#   Undo persistence   : source scripts/set_srn_prompt.sh --remove
#
# It must be SOURCED (not run with ./) so it can change your current shell.

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "Run it with 'source', e.g.:  source scripts/set_srn_prompt.sh YOUR_SRN"
    exit 1
fi

_cc_begin='# >>> CampusCompute SRN prompt >>>'
_cc_end='# <<< CampusCompute SRN prompt <<<'

if [[ "$1" == "--remove" ]]; then
    sed -i "/$_cc_begin/,/$_cc_end/d" ~/.bashrc
    echo "Removed SRN prompt from ~/.bashrc (open a new terminal to see the default prompt)."
    return 0
fi

if [[ -z "$1" ]]; then
    echo "Usage: source scripts/set_srn_prompt.sh YOUR_FULL_SRN [--persist]"
    return 1
fi
if [[ ! "$1" =~ ^[A-Za-z0-9]+$ ]]; then
    echo "Error: SRN should contain only letters and digits, got '$1'"
    return 1
fi

export SRN="${1^^}"
export PS1='[$SRN] \w\$ '

if [[ "$2" == "--persist" ]]; then
    sed -i "/$_cc_begin/,/$_cc_end/d" ~/.bashrc      # replace any earlier block
    {
        echo "$_cc_begin"
        echo "export SRN='$SRN'"
        echo "export PS1='[\$SRN] \\w\\\$ '"
        echo "$_cc_end"
    } >> ~/.bashrc
    echo "Saved to ~/.bashrc - every new terminal will show [$SRN]."
fi
echo "Prompt now shows: [$SRN]"
