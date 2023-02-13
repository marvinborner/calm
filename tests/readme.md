# Tests

These tests don't really test *specific* transition rules although I
used them to fix specific rules using TDD. They are still useful as
*random* overall reduction tests or for general benchmarking.

## Descriptions

1.  Smallest expression that uses all transition rules\[0\]
2.  Folds a list of balanced ternary numbers:
    `foldl + 0 [1, 2, 3] ~~> 6`
3.  Factorial function using church numerals and the z combinator
4.  Appends two lists of balanced ternary numbers:
    `[1, 2, 3] ++ [4] ~~> [1, 2, 3, 4]`
5.  Inifinite lazy list generators, list-based string representation and
    generator management using balanced ternary numbers. Basically
    equivalent of `(take (+6) (cycle "ab")) ~~> "ababab"`
6.  Stress test using factorial equalities, originally by Lennart
    Augustsson
