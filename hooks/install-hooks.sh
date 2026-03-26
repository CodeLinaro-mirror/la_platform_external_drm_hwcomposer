#!/bin/bash

# Install hooks.
git config --replace-all hook.pre-push.command "[ ! -d hooks ] || hooks/check-non-public-commits"
