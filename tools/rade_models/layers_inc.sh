#!/bin/sh
# Prints L(<var>, <field>) for each LinearLayer and C(<var>, <field>) for each
# Conv2dLayer in the struct <name> in <header> (written as
# "typedef struct {...} <name>;" or "struct <name> {...};"), for q8gen's
# layer tables.
# Usage: layers_inc.sh <header> <struct name> <var>
awk -v name="$2" -v var="$3" '
    /^(typedef )?struct/ { n = 0; tagged = ($0 ~ "struct " name " *\\{"); next }
    /^ *LinearLayer [A-Za-z0-9_]+;/ { gsub(/[ ;]/, "", $2); k[n] = "L"; f[n++] = $2; next }
    /^ *Conv2dLayer [A-Za-z0-9_]+;/ { gsub(/[ ;]/, "", $2); k[n] = "C"; f[n++] = $2; next }
    /^}/ {
        if (tagged || $0 ~ "^} *" name ";") for (i = 0; i < n; i++) printf "%s(%s, %s)\n", k[i], var, f[i]
        n = 0; tagged = 0
    }
' "$1"
