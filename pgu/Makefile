.DEFAULT_GOAL := shell

BUILD_DOCS ?= 1
USE_GRAPHICS ?= 1

TMUX_FILE := $(HOME)/.tmux.conf
TMUX_REAL_PATH := $(shell readlink -f $(TMUX_FILE))
TMUX_MOUNT := $(shell if [ -f $(TMUX_REAL_PATH) ]; then echo "-v $(TMUX_REAL_PATH):/root/.tmux.conf:Z" ; fi)


CONTAINER_CMD = podman
CONTAINER_NAME = programmingfromthegroundup
FILES_TO_MOUNT = -v ./docs:/pgu/docs:Z \
                 -v ./src:/pgu/src:Z \
                 -v ./entrypoint/shell.sh:/usr/local/bin/shell.sh:Z \
                 -v ./entrypoint/format.sh:/usr/local/bin/format.sh:Z \
                 -v ./entrypoint/pdf.sh:/usr/local/bin/pdf.sh:Z \
                 -v ./entrypoint/epub.sh:/usr/local/bin/epub.sh:Z \
                 -v ./entrypoint/html.sh:/usr/local/bin/html.sh:Z \
                 -v ./entrypoint/dotfiles/.extrabashrc:/root/.extrabashrc:Z \
                 $(TMUX_MOUNT)


OUTPUT_DIR_TO_MOUNT = -v ./output/:/output/:Z

USE_X = -e DISPLAY=$(DISPLAY) \
	-v /tmp/.X11-unix:/tmp/.X11-unix \
	--security-opt label=type:container_runtime_t
WAYLAND_FLAGS_FOR_CONTAINER = -e "WAYLAND_DISPLAY=${WAYLAND_DISPLAY}" \
                              -e "XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR}" \
                              -v "${XDG_RUNTIME_DIR}:${XDG_RUNTIME_DIR}"



.PHONY: all
all: shell ## Build the image and get a shell in it

.PHONY: image
image: ## Build podman image to run the examples
	# build the container
	$(CONTAINER_CMD) build \
                         --build-arg BUILD_DOCS=$(BUILD_DOCS) \
                         --build-arg USE_GRAPHICS=$(USE_GRAPHICS) \
                         -t $(CONTAINER_NAME) \
                         .


.PHONY: shell
shell: format  ## Get Shell into a ephermeral container made from the image
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(OUTPUT_DIR_TO_MOUNT) \
		$(USE_X) \
		$(WAYLAND_FLAGS_FOR_CONTAINER) \
		$(CONTAINER_NAME) \
		/usr/local/bin/shell.sh


.PHONY: format
format: image ## Format the C code
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(OUTPUT_DIR_TO_MOUNT) \
		$(USE_X) \
		$(CONTAINER_NAME) \
		/usr/local/bin/format.sh

.PHONY: docs
docs: html pdf epub ## Build the book in HTML, PDF, and EPUB form

.PHONY: html
html: image ## Build the book in HTML form
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(OUTPUT_DIR_TO_MOUNT) \
		$(CONTAINER_NAME) \
		/usr/local/bin/html.sh

.PHONY: pdf
pdf: image  ## Build the book in PDF form
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(OUTPUT_DIR_TO_MOUNT) \
		$(CONTAINER_NAME) \
		/usr/local/bin/pdf.sh

.PHONY: epub
epub: image  ## Build the book in EPUB form
	$(CONTAINER_CMD) run -it --rm \
		--entrypoint /bin/bash \
		$(FILES_TO_MOUNT) \
		$(OUTPUT_DIR_TO_MOUNT) \
		$(CONTAINER_NAME) \
		/usr/local/bin/epub.sh



.PHONY: help
help:
	@grep --extended-regexp '^[a-zA-Z0-9_-]+:.*?## .*$$' $(MAKEFILE_LIST) | sort | awk 'BEGIN {FS = ":.*?## "}; {printf "\033[36m%-30s\033[0m %s\n", $$1, $$2}'
