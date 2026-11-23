---
title: Sample
tags: [a, b]
---

Setext Heading
==============

# Markdown Viewer *test* `doc`

A paragraph with **bold**, *italic*, ***both***, ~~strike~~, `code span`, ==mark==,
a [link](https://example.com "title"), a ref [link][ref], <https://auto.link>,
and a bare URL https://github.com/foo_bar/baz_qux (with parens). Snake_case_word stays.
Escaped \*not italic\* and entities &copy; &amp; &#x1F600; &mdash; Árvíztűrő tükörfúrógép.
Hard break here  
next line.<br>After br.

## Lists

- item one
- item **two**
  continued lazily
  - nested *a*
  - nested b
    1. deep one
    2. deep two
- [ ] task open
- [x] task done

1. first
2. second

   loose paragraph in second

3) other delimiter list

> Quote with **bold**
> > nested quote
>
> - list in quote

```cpp
int main() {
    return 0; // {braces} \backslash
}
```

    indented code

| Left | Center | Right |
|:-----|:------:|------:|
| a    | `b|c`  | 1     |
| **x**| y      | 22    |

***

<p align="center"><img src="logo.png" alt="Logo"> <b>Bold html</b></p>

<!-- hidden comment -->

Footnote ref[^1] and image ![alt text](img.png) and linked badge [![CI](https://ci/badge.svg)](https://ci).

[ref]: https://ref.example.com
[^1]: The footnote.
