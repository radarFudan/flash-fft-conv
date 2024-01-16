FROM nvcr.io/nvidia/pytorch:23.05-py3

ARG DEBIAN_FRONTEND=noninteractive

COPY apt_install.txt .
RUN ln -sf /usr/bin/python3 /usr/bin/python
RUN pip3 install --upgrade pip

COPY cuda_12.1.0_530.30.02_linux.run .
RUN chmod +x cuda_12.1.0_530.30.02_linux.run
RUN ./cuda_12.1.0_530.30.02_linux.run --silent --toolkit --toolkitpath=/usr/local/cuda-12.1 --override
RUN nvcc --version  # 12.1
RUN nvidia-smi # 12.1

# Python packages
COPY requirements.txt .
RUN pip3 install -r requirements.txt --upgrade

RUN git clone https://github.com/HazyResearch/flash-fft-conv.git
RUN cd flash-fft-conv && \
    cd csrc/flashfftconv && \
    python setup.py install && \
    cd ../../.. && \
    python setup.py install

RUN pytest -s -q tests/test_flashfftconv.py # Require 40G card

