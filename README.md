long-shebang
=============

A tool for `#!` scripts with more than one argument.

Usage
------

```
#!/usr/bin/env long-shebang
#!sh -e
false
true
```

will fail with exit code 1

Escapes
--------

The following escapes are recognized in the second `#!` line:

* `\\`: A literal `\`
* `\n`: A newline
* `\ `: A space rather than an argument delimiter
