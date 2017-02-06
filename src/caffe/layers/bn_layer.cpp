#include <algorithm>
#include <vector>

#include "caffe/layers/batch_norm_v0_layer.hpp"
#include "caffe/filler.hpp"
#include "caffe/util/math_functions.hpp"

namespace caffe {
  template <typename Dtype>
  void BNLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    top[0]->Reshape(bottom[0]->num(), bottom[0]->channels(),
        bottom[0]->height(), bottom[0]->width());

//    x_norm_.Reshape(bottom[0]->num(), bottom[0]->channels(),
//        bottom[0]->height(), bottom[0]->width());

    // Figure out the dimensions
    N_ = bottom[0]->num();
    C_ = bottom[0]->channels();
    H_ = bottom[0]->height();
    W_ = bottom[0]->width();

    // mean
    spatial_mean_.Reshape(N_, C_, 1, 1);
    batch_mean_.Reshape(1, C_, 1, 1);
    // variance
    spatial_variance_.Reshape(N_, C_, 1, 1);
    batch_variance_.Reshape(1, C_, 1, 1);
    // buffer blod
    buffer_blob_.Reshape(N_, C_, H_, W_);

    // fill spatial multiplier
    spatial_sum_multiplier_.Reshape(1, 1, H_, W_);
    Dtype* spatial_multipl_data = spatial_sum_multiplier_.mutable_cpu_data();
    caffe_set(spatial_sum_multiplier_.count(), Dtype(1),
        spatial_multipl_data);
    caffe_set(spatial_sum_multiplier_.count(), Dtype(0),
        spatial_sum_multiplier_.mutable_cpu_diff());
    // fill batch multiplier
    batch_sum_multiplier_.Reshape(N_, 1, 1, 1);
    Dtype* batch_multiplier_data = batch_sum_multiplier_.mutable_cpu_data();
    caffe_set(batch_sum_multiplier_.count(), Dtype(1),
        batch_multiplier_data);
    caffe_set(batch_sum_multiplier_.count(), Dtype(0),
        batch_sum_multiplier_.mutable_cpu_diff());
    this->param_propagate_down_.resize(this->blobs_.size(), true);
  }
  template <typename Dtype>
  void BNLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    CHECK_NE(top[0], bottom[0]) << this->type() << " Layer does not "
    "allow in-place computation.";

    top[0]->Reshape(bottom[0]->num(), bottom[0]->channels(),
        bottom[0]->height(), bottom[0]->width());

    // Figure out the dimensions
    N_ = bottom[0]->num();
    C_ = bottom[0]->channels();
    H_ = bottom[0]->height();
    W_ = bottom[0]->width();
    var_eps_ = 1e-9;
    bn_memory_ = this->layer_param_.bn_param().bn_memory();

    // mean
    spatial_mean_.Reshape(N_, C_, 1, 1);
    batch_mean_.Reshape(1, C_, 1, 1);
    // variance
    spatial_variance_.Reshape(N_, C_, 1, 1);
    batch_variance_.Reshape(1, C_, 1, 1);
    // buffer blod
    buffer_blob_.Reshape(N_, C_, H_, W_);

    // fill spatial multiplier
    spatial_sum_multiplier_.Reshape(1, 1, H_, W_);
    Dtype* spatial_multipl_data = spatial_sum_multiplier_.mutable_cpu_data();
    caffe_set(spatial_sum_multiplier_.count(), Dtype(1),
        spatial_multipl_data);
    caffe_set(spatial_sum_multiplier_.count(), Dtype(0),
        spatial_sum_multiplier_.mutable_cpu_diff());

    // fill batch multiplier
    batch_sum_multiplier_.Reshape(N_, 1, 1, 1);
    Dtype* batch_multiplier_data = batch_sum_multiplier_.mutable_cpu_data();
    caffe_set(batch_sum_multiplier_.count(), Dtype(1),
        batch_multiplier_data);
    caffe_set(batch_sum_multiplier_.count(), Dtype(0),
        batch_sum_multiplier_.mutable_cpu_diff());

    // Check if we need to set up the weights
    if (this->blobs_.size() > 0) {
      LOG(INFO) << "Skipping parameter initialization";
    } else {
      this->blobs_.resize(4);

      // fill scale with scale_filler
      this->blobs_[0].reset(new Blob<Dtype>(1, C_, 1, 1));
      shared_ptr<Filler<Dtype> > scale_filler(GetFiller<Dtype>(
          this->layer_param_.bn_param().scale_filler()));
      scale_filler->Fill(this->blobs_[0].get());

      // fill shift with shift_filler
      this->blobs_[1].reset(new Blob<Dtype>(1, C_, 1, 1));
      shared_ptr<Filler<Dtype> > shift_filler(GetFiller<Dtype>(
          this->layer_param_.bn_param().shift_filler()));
      shift_filler->Fill(this->blobs_[1].get());
      
      // init running mean and variance
      this->blobs_[2].reset(new Blob<Dtype>(1, C_, 1, 1));
      caffe_set(C_, Dtype(0),
        this->blobs_[2]->mutable_cpu_data());

      this->blobs_[3].reset(new Blob<Dtype>(1, C_, 1, 1));
      caffe_set(this->blobs_[3]->count(), Dtype(0),
        this->blobs_[2]->mutable_cpu_data());
    }  // parameter initialization
    this->param_propagate_down_.resize(this->blobs_.size(), true);
  }

  template <typename Dtype>
  void BNLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    const Dtype* bottom_data = bottom[0]->cpu_data();
    Dtype* top_data = top[0]->mutable_cpu_data();
    const Dtype* const_top_data = top[0]->cpu_data();  
    
    const Dtype* scale_data = this->blobs_[0]->cpu_data();
    const Dtype* shift_data = this->blobs_[1]->cpu_data();
  
    if(this->phase_ == TRAIN)
    {
        // put the squares of bottom into buffer_blob_
        caffe_powx(bottom[0]->count(), bottom_data, Dtype(2),
            buffer_blob_.mutable_cpu_data());
    
        // computes variance using var(X) = E(X^2) - (EX)^2
        // EX across spatial
        caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_, H_ * W_,
            Dtype(1. / (H_ * W_)), bottom_data,
            spatial_sum_multiplier_.cpu_data(), Dtype(0),
            spatial_mean_.mutable_cpu_data());
        // EX across batch
        caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_, Dtype(1. / N_),
            spatial_mean_.cpu_data(),
            batch_sum_multiplier_.cpu_data(), Dtype(0),
            batch_mean_.mutable_cpu_data());
    
        // E(X^2) across spatial
        caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_, H_ * W_,
            Dtype(1. / (H_ * W_)), buffer_blob_.cpu_data(),
            spatial_sum_multiplier_.cpu_data(), Dtype(0),
            spatial_variance_.mutable_cpu_data());
        // E(X^2) across batch
        caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_, Dtype(1. / N_),
            spatial_variance_.cpu_data(),
            batch_sum_multiplier_.cpu_data(), Dtype(0),
            batch_variance_.mutable_cpu_data());
    
        caffe_powx(batch_mean_.count(), batch_mean_.cpu_data(), Dtype(2),
            buffer_blob_.mutable_cpu_data());  // (EX)^2
        caffe_sub(batch_mean_.count(), batch_variance_.cpu_data(),
            buffer_blob_.cpu_data(),
            batch_variance_.mutable_cpu_data());  // variance
            
        // update  2015-08-28 MLX
        caffe_cpu_axpby(this->blobs_[2]->count(), Dtype(1)-bn_memory_, batch_mean_.cpu_data(), 
            bn_memory_, this->blobs_[2]->mutable_cpu_data());
        
        caffe_cpu_axpby(this->blobs_[3]->count(), Dtype(1)-bn_memory_, batch_variance_.cpu_data(), 
            bn_memory_, this->blobs_[3]->mutable_cpu_data());
                
    }
    else
    {
        // cal mean            
        caffe_cpu_axpby(batch_mean_.count(), Dtype(1), this->blobs_[2]->cpu_data(),
            Dtype(0), batch_mean_.mutable_cpu_data());
        
        // cal variance
        caffe_cpu_axpby(batch_variance_.count(), Dtype(1), this->blobs_[3]->cpu_data(),
            Dtype(0), batch_variance_.mutable_cpu_data()); 
            
//        printf("mlx: %f\n", bn_memory_);    
//        const Dtype* mmdata =  batch_variance_.cpu_data();
//        for(int i = 0; i < this->blobs_[2]->count(); i ++)
//        {
//            printf("%f ", mmdata[i]);//mmdata[i]
//        }
//        printf("\n");
    }
    
    // do mean and variance normalization
    // subtract mean
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(-1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());

    caffe_add(buffer_blob_.count(), bottom_data,
        buffer_blob_.cpu_data(), top_data);

    // normalize variance
    caffe_add_scalar(batch_variance_.count(), var_eps_,
        batch_variance_.mutable_cpu_data());
    caffe_powx(batch_variance_.count(),
        batch_variance_.cpu_data(), Dtype(0.5),
        batch_variance_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_variance_.cpu_data(), Dtype(0),
        spatial_variance_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_ * C_, H_ * W_, 1, Dtype(1),
        spatial_variance_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());

    caffe_div(buffer_blob_.count(), const_top_data,
        buffer_blob_.cpu_data(), top_data);

    // Saving x_norm
    caffe_copy(buffer_blob_.count(), const_top_data,
        buffer_blob_.mutable_cpu_diff());
    // scale
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(), scale_data, Dtype(0),
        spatial_variance_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(1),
        spatial_variance_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
    caffe_mul(buffer_blob_.count(), top_data,
        buffer_blob_.cpu_data(), top_data);

    // shift
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(), shift_data, Dtype(0),
        spatial_mean_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_ * C_, H_ * W_, 1, Dtype(1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
    caffe_add(buffer_blob_.count(), const_top_data,
        buffer_blob_.cpu_data(), top_data);
  }
  
#if 1

  template <typename Dtype>
  void BNLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down,
      const vector<Blob<Dtype>*>& bottom) {
    const Dtype* top_diff = top[0]->cpu_diff();
    const Dtype* bottom_data = bottom[0]->cpu_data();
    Dtype* bottom_diff = bottom[0]->mutable_cpu_diff();

    Dtype* scale_diff = this->blobs_[0]->mutable_cpu_diff();
    Dtype* shift_diff = this->blobs_[1]->mutable_cpu_diff();
    const Dtype* scale_data = this->blobs_[0]->cpu_data();

// Propagate layer to parameters
    // gradient w.r.t. scale
    caffe_mul(buffer_blob_.count(), buffer_blob_.cpu_diff(),
        top_diff, buffer_blob_.mutable_cpu_data());
    // EX across spatial
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), buffer_blob_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        spatial_variance_.mutable_cpu_diff());
    // EX across batch
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_, Dtype(1),
        spatial_variance_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(), Dtype(0), scale_diff);

    // gradient w.r.t. shift
    // EX across spatial
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), top_diff,
        spatial_sum_multiplier_.cpu_data(),
        Dtype(0), spatial_mean_.mutable_cpu_diff());
    // EX across batch
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_,
        Dtype(1), spatial_mean_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(),
        Dtype(0), shift_diff);

// Propagate down

    // put scale * top_diff to buffer_blob_
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(), scale_data, Dtype(0),
        spatial_variance_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(1),
        spatial_variance_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
    caffe_mul(buffer_blob_.count(), top_diff, buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());

    // use new top diff for computation
    caffe_mul(buffer_blob_.count(),  buffer_blob_.cpu_diff(),
        buffer_blob_.cpu_data(), bottom_diff);
    // EX across spatial
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_, H_ * W_,
        Dtype(1), bottom_diff,
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());
    // EX across batch
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_, Dtype(1),
        spatial_mean_.cpu_data(),
        batch_sum_multiplier_.cpu_data(), Dtype(0),
        batch_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        bottom_diff);

    caffe_mul(buffer_blob_.count(),
        buffer_blob_.cpu_diff(), bottom_diff, bottom_diff);

    // EX across spatial
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), buffer_blob_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());
    // EX across batch
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_, Dtype(1),
        spatial_mean_.cpu_data(),
        batch_sum_multiplier_.cpu_data(), Dtype(0),
        batch_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_ * C_, H_ * W_, 1, Dtype(1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(1), bottom_diff);

    caffe_cpu_axpby(buffer_blob_.count(), Dtype(1),
        buffer_blob_.cpu_data(), Dtype(-1. / (N_ * H_ * W_)),
        bottom_diff);

    // put the squares of bottom into buffer_blob_
    caffe_powx(buffer_blob_.count(), bottom_data, Dtype(2),
        buffer_blob_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_variance_.cpu_data(), Dtype(0),
        spatial_variance_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_ * C_, H_ * W_, 1, Dtype(1),
        spatial_variance_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());

    caffe_div(buffer_blob_.count(), bottom_diff,
        buffer_blob_.cpu_data(), bottom_diff);
  }
  
#else
 
  template <typename Dtype>
  void BNLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down,
      const vector<Blob<Dtype>*>& bottom) {
    const Dtype* top_diff = top[0]->cpu_diff();
    const Dtype* bottom_data = bottom[0]->cpu_data();
    Dtype* bottom_diff = bottom[0]->mutable_cpu_diff();

    Dtype* scale_diff = this->blobs_[0]->mutable_cpu_diff();
    Dtype* shift_diff = this->blobs_[1]->mutable_cpu_diff();
    const Dtype* scale_data = this->blobs_[0]->cpu_data();

// Propagate layer to parameters
    // gradient w.r.t. scale
    caffe_mul(buffer_blob_.count(), x_norm_.cpu_data(),
        top_diff, buffer_blob_.mutable_cpu_data());
    // EX across spatial
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), buffer_blob_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        spatial_variance_.mutable_cpu_diff());
    // EX across batch
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_, Dtype(1),
        spatial_variance_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(), Dtype(0), scale_diff);

    // gradient w.r.t. shift
    // EX across spatial
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), top_diff,
        spatial_sum_multiplier_.cpu_data(),
        Dtype(0), spatial_mean_.mutable_cpu_diff());
    // EX across batch
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_,
        Dtype(1), spatial_mean_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(),
        Dtype(0), shift_diff);


    //printf("mlx1\n");
    
// Propagate down

    // put scale * top_diff to buffer_blob_
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_, C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(), scale_data, Dtype(0),
        spatial_variance_.mutable_cpu_data());
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(1),
        spatial_variance_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
    caffe_mul(buffer_blob_.count(), top_diff, buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());

    // use new top diff for computation
    caffe_cpu_axpby(buffer_blob_.count(),  Dtype(1), buffer_blob_.cpu_data(),
        Dtype(1), x_norm_.mutable_cpu_data());
        
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_variance_.cpu_data(), Dtype(0),
        spatial_variance_.mutable_cpu_data());
        
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans,
        N_ * C_, H_ * W_, 1, Dtype(1),
        spatial_variance_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
        
    // new top diff div sqrt(batch_var + eps)
    caffe_div(buffer_blob_.count(), x_norm_.cpu_data(),
        buffer_blob_.cpu_data(), bottom_diff);
    
    //printf("mlx2\n");
        
    //cal variance gradient        
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(-1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
        
    caffe_add(buffer_blob_.count(), bottom_data,
        buffer_blob_.cpu_data(), buffer_blob_.mutable_cpu_data());
        
    caffe_mul(buffer_blob_.count(), x_norm_.cpu_data(), buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());
    
    //printf("mlx2_1\n");
        
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), buffer_blob_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(),
        Dtype(0), spatial_mean_.mutable_cpu_diff());
    
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_,
        Dtype(1), spatial_mean_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(),
        Dtype(0), g_batch_variance_.mutable_cpu_data());
        
    //printf("mlx2_2\n");
        
    caffe_powx(batch_variance_.count(), batch_variance_.cpu_data(), Dtype(3),
        buffer_blob_.mutable_cpu_data());
    
    caffe_axpy(buffer_blob_.count(),  Dtype(-0.5), buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());
        
    //printf("mlx2_3[%d, %d]\n", buffer_blob_.count(), g_batch_variance_.count());
    
    caffe_div(g_batch_variance_.count(), g_batch_variance_.cpu_data(), buffer_blob_.cpu_data(),
        g_batch_variance_.mutable_cpu_data());
        
    //printf("mlx3\n");
    
    //cal mean gradient
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1), x_norm_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(),
        Dtype(0), spatial_mean_.mutable_cpu_diff());
    
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_,
        Dtype(1), spatial_mean_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(),
        Dtype(0), buffer_blob_.mutable_cpu_data());
    
    caffe_axpy(buffer_blob_.count(),  Dtype(-1), buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());
    
    caffe_div(batch_variance_.count(), buffer_blob_.cpu_data(), batch_variance_.cpu_data(),
        g_batch_mean_.mutable_cpu_data());
    
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(-1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
        
    caffe_add(buffer_blob_.count(), bottom_data,
        buffer_blob_.cpu_data(), buffer_blob_.mutable_cpu_data());
    
    caffe_axpy(buffer_blob_.count(),  Dtype(-2), buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());
    
    caffe_cpu_gemv<Dtype>(CblasNoTrans, N_ * C_,
        H_ * W_, Dtype(1. / (H_ * W_)), buffer_blob_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(),
        Dtype(0), spatial_mean_.mutable_cpu_diff());
    
    caffe_cpu_gemv<Dtype>(CblasTrans, N_, C_,
        Dtype(1. / N_), spatial_mean_.cpu_diff(),
        batch_sum_multiplier_.cpu_data(),
        Dtype(0), buffer_blob_.mutable_cpu_data());
        
    caffe_mul(g_batch_variance_.count(), g_batch_variance_.cpu_data(),
        buffer_blob_.cpu_data(), buffer_blob_.mutable_cpu_data());    
        
    caffe_add(g_batch_mean_.count(), g_batch_mean_.cpu_data(),
        buffer_blob_.cpu_data(), g_batch_mean_.mutable_cpu_data());  
       
    //printf("mlx4\n");  
        
    //get bottom diff
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(-1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());
        
    caffe_add(buffer_blob_.count(), bottom_data,
        buffer_blob_.cpu_data(), buffer_blob_.mutable_cpu_data());   
        
    caffe_axpy(buffer_blob_.count(),  Dtype(2./(H_*W_*N_)), buffer_blob_.cpu_data(),
        buffer_blob_.mutable_cpu_data());           
    
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        g_batch_variance_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        x_norm_.mutable_cpu_data());
        
    caffe_mul(buffer_blob_.count(), x_norm_.cpu_data(),
        buffer_blob_.cpu_data(), buffer_blob_.mutable_cpu_data());
        
    caffe_axpy(buffer_blob_.count(), Dtype(1), buffer_blob_.cpu_data(),
        bottom_diff);     
        
    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_,
        C_, 1, Dtype(1),
        batch_sum_multiplier_.cpu_data(),
        g_batch_mean_.cpu_data(), Dtype(0),
        spatial_mean_.mutable_cpu_data());

    caffe_cpu_gemm<Dtype>(CblasNoTrans, CblasNoTrans, N_ * C_,
        H_ * W_, 1, Dtype(1),
        spatial_mean_.cpu_data(),
        spatial_sum_multiplier_.cpu_data(), Dtype(0),
        buffer_blob_.mutable_cpu_data());    
        
    caffe_axpy(buffer_blob_.count(), Dtype(1. / (H_*W_*N_)), buffer_blob_.cpu_data(),
        bottom_diff);    

  }
#endif  
  
#ifdef CPU_ONLY
STUB_cpu(BNLayer);
#endif

  INSTANTIATE_CLASS(BNLayer);
  REGISTER_LAYER_CLASS(BN);
}  // namespace caffe
