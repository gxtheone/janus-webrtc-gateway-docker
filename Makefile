TEMPLATE_NAME ?= janus-webrtc-gateway-docker-gx

build:
	@docker build -t atyenoria/$(TEMPLATE_NAME) .

build-nocache:
	@docker build --no-cache -t atyenoria/$(TEMPLATE_NAME) .

bash: 
	@docker run --rm --net=host --name="janus" -it -t atyenoria/$(TEMPLATE_NAME) /bin/bash

run: 
	@docker run --rm --net=host --name="janus" --ulimit core=-1 -it -t atyenoria/$(TEMPLATE_NAME)

run-mac: 
	@docker run --rm  -p 80:80 -p 8088:8088 -p 8188:8188 --name="janus-7788" --ulimit core=-1 -it -t atyenoria/$(TEMPLATE_NAME)

rung:
	@docker run --rm  -p 80:80 -p 8088:8088 -p 8188:8188 --name="janus-7788" --ulimit core=-1 -it -t atyenoria/$(TEMPLATE_NAME) bash

run-hide: 
	@docker run --rm --net=host --name="janus" -it -t atyenoria/$(TEMPLATE_NAME) >> /dev/null
