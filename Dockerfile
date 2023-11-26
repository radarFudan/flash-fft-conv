FROM nvcr.io/nvidia/pytorch:23.05-py3

ARG DEBIAN_FRONTEND=noninteractive

COPY apt_install.txt .
# RUN apt-get update && apt-get install -y --no-install-recommends `cat apt_install.txt` && rm -rf /var/lib/apt/lists/*
# RUN apt-get update && apt-get install -y --no-install-recommends `cat apt_install.txt`
# RUN apt-get install -y git wget tmux tree

# Config pip
# RUN ln -sf /usr/bin/python3.9 /usr/bin/python3
RUN ln -sf /usr/bin/python3 /usr/bin/python

# Upgrade pip, install py libs
RUN pip3 install --upgrade pip

# Python packages
COPY requirements.txt .
RUN pip3 install -r requirements.txt --upgrade

# Prepare the package
RUN git clone https://github.com/HazyResearch/flash-fft-conv.git
RUN nvcc --version  # 12.1
RUN nvidia-smi # 12.1
RUN cd flash-fft-conv && \
    cd csrc/flashfftconv && \
    python setup.py install && \
    cd ../../.. && \
    python setup.py install

RUN pytest -s -q tests/test_flashfftconv.py # Require 40G card
