FROM docker.io/debian:trixie

RUN apt update && apt upgrade -y && \
    apt install    -y texlive \
                      texlive-latex-extra \
                      python3-sphinx-rtd-theme \
                      fonts-mathjax \
                      libjs-mathjax \
                      make \
                      aspell-en

COPY ./book /book/
COPY ./src /src/
COPY ./entrypoint/entrypoint.sh  /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
