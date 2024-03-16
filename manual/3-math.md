# Math rendering support

Pandoc supports math rendering in HTML using different engines. We recommend to use the [KaTeX](https://katex.org/) engine, specified by the `--katex` option on the command-line.

In-line math equations like $U = R \cdot I$ must be wrapped within two `$`-signs, without a space after the starting `$` and without a space before the ending `$`.

Math equations in a separate line are defined using two `$$`. They are rendered like this:

$$\rho\dot{\vec{v}}
=\rho\left(\frac{\partial\vec{v}}{\partial t}+(\vec{v}\cdot\nabla)\vec{v}\right)
=-\nabla p+\mu\Delta\vec{v}+(\lambda+\mu)\nabla (\nabla\cdot\vec{v})+\vec{f}.$$
