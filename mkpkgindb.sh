#!/bin/sh

# $Id$

echo "/* automatically generated, DO NOT EDIT */"
echo '#define CREATE_DRYDB " \'
${SEDCMD} -E -e 's/$/ \\/' -e 's/\"/\\\"/g' pkgin.sql
echo '"' 
