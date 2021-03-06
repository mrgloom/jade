#ifdef USE_CUDNN
#include <algorithm>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
void CuDNNReLULayer<Dtype>::Forward_gpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  // Fallback to standard Caffe for leaky ReLU.
  if (ReLULayer<Dtype>::layer_param_.relu_param().negative_slope() != 0) {
    return ReLULayer<Dtype>::Forward_gpu(bottom, top);
  }

  const Dtype* bottom_data = bottom[0]->gpu_data();
  Dtype* top_data = top[0]->mutable_gpu_data();

  Dtype alpha = 1.0;
  Dtype beta  = 0.0;

  CUDNN_CHECK(cudnnActivationForward(this->handle_,
        CUDNN_ACTIVATION_RELU,
        reinterpret_cast<void *>(&alpha),
        this->bottom_desc_, bottom_data,
        reinterpret_cast<void *>(&beta),
        this->top_desc_, top_data));
}

template <typename Dtype>
void CuDNNReLULayer<Dtype>::Backward_gpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  if (!propagate_down[0]) {
    return;
  }

  // Fallback to standard Caffe for leaky ReLU.
  if (ReLULayer<Dtype>::layer_param_.relu_param().negative_slope() != 0) {
    return ReLULayer<Dtype>::Backward_gpu(top, propagate_down, bottom);
  }

  const Dtype* top_data = top[0]->gpu_data();
  const Dtype* top_diff = top[0]->gpu_diff();
  const Dtype* bottom_data = bottom[0]->gpu_data();
  Dtype* bottom_diff = bottom[0]->mutable_gpu_diff();

  Dtype alpha = 1.0;
  Dtype beta = 0.0;

  CUDNN_CHECK(cudnnActivationBackward(this->handle_,
        CUDNN_ACTIVATION_RELU,
        reinterpret_cast<void *>(&alpha),
        this->top_desc_, top_data, this->top_desc_, top_diff,
        this->bottom_desc_, bottom_data,
        reinterpret_cast<void *>(&beta),
        this->bottom_desc_, bottom_diff));
}

INSTANTIATE_LAYER_GPU_FUNCS(CuDNNReLULayer);

}  // namespace caffe
#endif
