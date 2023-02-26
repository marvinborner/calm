# calm

> **c**alm **a**bstract **l**ambda **m**achine

-   **Strong** reduction (reduction inside abstractions) of
    **call-by-need** lambda calculus
-   Originally intended as reducer of the
    [`bruijn`](https://github.com/marvinborner/bruijn) programming
    language
-   Useful for proof assistants or as a high-level lambda-term reducer
    of functional programming languages
-   Based on bleeding-edge research results
-   Mostly linear time/memory complexity\[0\]

## Garbage collection

In theory the RKNL abstract machine should be able to be implementented
without a periodic/incremental garbage collector.

I tried implementing it using reference counting but eventually gave up.
You can find my attempts at
[a0b4299cbd](https://github.com/marvinborner/calm/tree/a0b4299cbda261684ad464b22c07a07bcf3acbed).

## Libraries

-   [CHAMP](https://github.com/ammut/immutable-c-ollections) \[MIT\]:
    Underrated efficient hash array mapped trie
-   [BDWGC](https://github.com/ivmai/bdwgc) \[MIT\]: Boehm-Demers-Weiser
    Garbage Collector

## Research

The base of this project is the RKNL\[0\] abstract machine. Other
interesting/relevant research in no particular order:

0.  Biernacka, M., Charatonik, W., & Drab, T. (2022). A simple and
    efficient implementation of strong call by need by an abstract
    machine. Proceedings of the ACM on Programming Languages, 6(ICFP),
    109-136.
1.  Accattoli, B., Condoluci, A., & Coen, C. S. (2021, June). Strong
    call-by-value is reasonable, implosively. In 2021 36th Annual
    ACM/IEEE Symposium on Logic in Computer Science (LICS) (pp. 1-14).
    IEEE.
2.  Balabonski, T., Lanco, A., & Melquiond, G. (2021). A strong
    call-by-need calculus. arXiv preprint arXiv:2111.01485.
3.  Accattoli, B., Condoluci, A., Guerrieri, G., & Coen, C. S. (2019,
    October). Crumbling abstract machines. In Proceedings of the 21st
    International Symposium on Principles and Practice of Declarative
    Programming (pp. 1-15).
4.  Accattoli, B., & Coen, C. S. (2015, July). On the relative
    usefulness of fireballs. In 2015 30th Annual ACM/IEEE Symposium on
    Logic in Computer Science (pp. 141-155). IEEE.
5.  Condoluci, A., Accattoli, B., & Coen, C. S. (2019, October). Sharing
    equality is linear. In Proceedings of the 21st International
    Symposium on Principles and Practice of Declarative Programming
    (pp. 1-14).
6.  Accattoli, B., & Leberle, M. (2021). Useful open call-by-need. arXiv
    preprint arXiv:2107.06591.
