# Contributor guide for WideMelon

WideMelon is a modified melonDS application with shared upstream Git history.
Report WideMelon problems and submit WideMelon changes here rather than to the
melonDS project unless the issue has been reproduced in unmodified melonDS.

Build and run the complete automated suite before submitting engine, renderer,
shader, Qt, dependency, or packaging changes:

```sh
./scripts/build.sh
```

For the faster profile and protocol test loop:

```sh
cmake -S tests -B build/tests -G Ninja
cmake --build build/tests
ctest --test-dir build/tests --output-on-failure
```

Keep changes focused and preserve melonDS behavior outside the WideMelon
feature. Rendering changes also require manual testing: the automated suite
contains no ROM and cannot validate game compatibility or shader output.

## Updating melonDS

Upstream updates are deliberate, reviewed merges rather than automatic pulls.
Maintainers should add the canonical upstream remote once:

```sh
git remote add upstream https://github.com/melonDS-emu/melonDS.git
```

Perform each update on its own branch, record the selected upstream commit in
the merge message, resolve source-level conflicts, and run the complete build,
packaging, and manual smoke-test workflow before merging it into `main`.

## Style inherited from melonDS

Please follow a style as documented here. Note that this guide was not always enforced, so some parts of the code violate it.

Files should use 4 spaces for indentation.

```c++

// for single line comments prefer C++ style

/*
    for multiline comments
    both C style comments
*/
// as well as
// C++ style comments are possible

// when including headers from the C standard library use their C names (so don't use cstdio/cstring, ...)
#include <stdio.h>

// namespaces in PascalCase
namespace Component
{ // for all constructs curly braces go onto the following line

// the content of namespaces should not be indented

int GlobalVariable; // in PascalCase

// function names should use PascalCase, parameters camelCase:
void Test(int someParam)
{
    int variable = someParam * 2; // local variables in camelCase
    
    // you can slightly vary the spacing around operators:
    int variable2 = someParam*2 + 1;
    // but avoid something like this: someParam* 2+ 3

    for (int i = 0; i < variable; i++) // always a space between if/for/while and the braces
    {
        // not using curly braces is allowed
        // if the body of the if/for/while is simple:
        if ((i % 2) == 0)
            printf("%d\n", i); // no space between the function name and the braces
    }
}

// defines should always be in CAPITALISED_SNAKE_CASE
#ifdef SOME_CONFIGURATION

// the content of #ifdef sections should not be indented
// the only exception being otherwise hard to read nested sections of #ifdef/#if/#defines
// as e.g. in ARMJIT_Memory.cpp

// prefer #ifdef/#ifndef, only use if defined(...) for complex expressions like this:
#if defined(THIS) || defined(THAT)
// ...
#endif

class MyClass // PascalCase
{
public: // access specfications are not indented
    void Test(int param) // for methods the same rules apply as for functions
    {
    }

private:
    int MemberVariable; // PascalCase, no prefix
};

#endif

#endif

enum
{
    // enums should always have a common prefix in camelCase
    // separated by an underscore with the item name
    // which has to be in PascalCase
    enumPrefix_FirstElement,
    enumPrefix_SecondElement,
    enumPrefix_ThirdElement,
    enumPrefix_FourthElement,
};

}

```

Some additional notes:

* Keep the definition and initialisation of local variables in one place and keep the scope of local variables as small as possible.

**That means avoid code like this**:
```cpp
void ColorConvert(u32* dst, u16* vram)
{
    u16 color;
    u8 r, g, b;
    int i;

    for (i = 0; i < 256; i++)
    {
        color = vram[i];
        r = (color & 0x001F) << 1;
        g = (color & 0x03E0) >> 4;
        b = (color & 0x7C00) >> 9;

        dst[i] = r | (g << 8) | (b << 16);
    }
}
```

**Do this instead:**
```cpp
void ColorConvert(u32* dst, u16* vram)
{
    for (int i = 0; i < 256; i++)
    {
        u16 color = vram[i];
        u8 r = (color & 0x001F) << 1;
        u8 g = (color & 0x03E0) >> 4;
        u8 b = (color & 0x7C00) >> 9;

        dst[i] = r | (g << 8) | (b << 16);
    }
}
```

* For integer types preferably use the explictly typed ones. We have short aliases for them defined in types.h (for unsigned types: `u8`, `u16`, `u32`, `u16`. For signed `s8`, `s16`, `s32`, `s64`). In some situations like loop variables, using `int` is possible as well.
* Don't overdo object oriented programming. Always try to use a simpler construct first, only use a polymorphic class if a namespace with functions in it doesn't cut it.

* In doubt put a namespace around your part of the code.

* C style strings (and the associated functions from the C standard library) are used in most places. We are thinking about changing this, as C strings are a bit of a hassle to deal with, but for the time being this is what we use.

* Only the C standard IO is used (so use `printf`, `fopen`, … Do not use `std::cout`/`std::ostream`, …).

* Complex C++ containers can be used (`std::vector`, `std::list`, `std::map`, …). `std::array` is usually not used, unless necessary so that the container can be used with other C++ constructs (e.g. `<algorithm>`). Only use them if a C array doesn't cut it.

* File names should be in PascalCase

* If a header file is called MyHeader.h it should be guarded with an ifdef like this:
```cpp
#ifndef MYHEADER_H
#define MYHEADER_H
// ...
#endif
```

* If you have questions, open a WideMelon discussion or issue.
