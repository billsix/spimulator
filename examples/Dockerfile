FROM registry.fedoraproject.org/fedora:44

RUN sed -i -e "s@tsflags=nodocs@#tsflags=nodocs@g" /etc/dnf/dnf.conf && \
    echo "keepcache=True" >> /etc/dnf/dnf.conf && \
    dnf upgrade -y && \
    dnf install -y aspell \
                   aspell-en \
                   git \
                   make \
                   python3-sphinx \
                   python3-sphinx_rtd_theme \
                   texlive \
                   texlive-collection-latexextra

COPY ./book /book/
COPY ./src /src/
COPY ./entrypoint/entrypoint.sh  /entrypoint.sh

ENTRYPOINT ["/entrypoint.sh"]
