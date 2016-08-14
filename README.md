long-shebang
=============

A tool for `#!` scripts with more than one argument.

On [most][1] modern systems, the kernel will only pass one argument to
a shebang interpreter script. Some tools, like `perl` and `ruby`, work
around this by parsing the shebang line themselves. `long-shebang` is a
more general solution: it will parse the *second* line as a shebang
line, with arbitrary numbers of arguments. As an additional convenience,
the program to be run can be looked up in `PATH`.

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
* `\ `(backslash-space): A space rather than an argument delimiter
* `\a`: If this is the first argument on the second `#!` line,
  don't treat the program name as `argv[0]`. For example, a script
  with `#!\a sh bash -e` will be equivalent to
  `execvp("sh", {"bash", "-e", script, NULL})` rather than
  `execvp("sh", {"sh", "bash", "-e", script, NULL})`.

[1]: http://www.in-ulm.de/~mascheck/various/shebang/
