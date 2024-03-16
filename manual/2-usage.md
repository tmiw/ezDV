# Usage

## Folder structure

Everything you need is stored in the `manual` subfolder. This makes sure it is easy to integrate into existing projects. Just copy the folder from this example to your own repository.

## Pandoc metadata

Some advanced configuration for pandoc is stored in the file `metadata.txt`.

## Markdown input

The markdown file names should be numbered according to the order in the final document. Header numbers are generated automatically by pandoc and should not be added in the markdown files.

## HTML generation

The HTML template is based on the great [mdBook](https://github.com/rust-lang-nursery/mdBook) theme, which was simplified and adjusted a bit to suit the needs of a manual.

In the `manual` subfolder call:

   pandoc metadata.txt *.md -o manual.html --template template/mdbook.html --from markdown --listings --number-sections --toc --toc-depth=2 --katex

## PDF generation

PDF files are generated using LaTeX, so a working LaTeX engine needs to be installed on your system already (e.g. texlive). The manual uses the [Eisvogel](https://github.com/Wandmalfarbe/pandoc-latex-template) template.

In the `manual` subfolder call:

   pandoc metadata.txt *.md -o manual.pdf --template template/eisvogel.tex --from markdown --listings --number-sections --toc
