#include <string>
#include <vector>

#include "boost/scoped_ptr.hpp"
#include "gtest/gtest.h"

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/data_layers.hpp"
#include "caffe/filler.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/db.hpp"
#include "caffe/util/io.hpp"

#include "caffe/test/test_caffe_main.hpp"

namespace caffe {

using boost::scoped_ptr;

template <typename TypeParam>
class DataLayerTest : public MultiDeviceTest<TypeParam> {
  typedef typename TypeParam::Dtype Dtype;

 protected:
  DataLayerTest()
      : backend_(DataParameter_DB_LEVELDB),
        blob_top_data_(new Blob<Dtype>()),
        blob_top_label_(new Blob<Dtype>()),
        seed_(1701) {}
  virtual void SetUp() {
    filename_.reset(new string());
    MakeTempDir(filename_.get());
    *filename_ += "/db";
    blob_top_vec_.push_back(blob_top_data_);
    blob_top_vec_.push_back(blob_top_label_);
  }

  // Fill the DB with data: if unique_pixels, each pixel is unique but
  // all images are the same; else each image is unique but all pixels within
  // an image are the same.
  void Fill(const bool unique_pixels, DataParameter_DB backend) {
    backend_ = backend;
    LOG(INFO) << "Using temporary dataset " << *filename_;
    scoped_ptr<db::DB> db(db::GetDB(backend));
    db->Open(*filename_, db::NEW);
    scoped_ptr<db::Transaction> txn(db->NewTransaction());
    for (int i = 0; i < 5; ++i) {
      Datum datum;
      datum.set_label(i);
      datum.set_channels(2);
      datum.set_height(3);
      datum.set_width(4);
      std::string* data = datum.mutable_data();
      for (int j = 0; j < 24; ++j) {
        int datum = unique_pixels ? j : i;
        data->push_back(static_cast<uint8_t>(datum));
      }
      stringstream ss;
      ss << i;
      string out;
      CHECK(datum.SerializeToString(&out));
      txn->Put(ss.str(), out);
    }
    txn->Commit();
    db->Close();
  }

  void TestRead() {
    const Dtype scale = 3;
    LayerParameter param;
    DataParameter* data_param = param.mutable_data_param();
    data_param->set_batch_size(5);
    data_param->set_source(filename_->c_str());
    data_param->set_backend(backend_);

    TransformationParameter* transform_param =
        param.mutable_transform_param();
    transform_param->set_scale(scale);

    DataLayer<Dtype> layer(param);
    layer.SetUp(blob_bottom_vec_, blob_top_vec_);
    EXPECT_EQ(blob_top_data_->num(), 5);
    EXPECT_EQ(blob_top_data_->channels(), 2);
    EXPECT_EQ(blob_top_data_->height(), 3);
    EXPECT_EQ(blob_top_data_->width(), 4);
    EXPECT_EQ(blob_top_label_->num(), 5);
    EXPECT_EQ(blob_top_label_->channels(), 1);
    EXPECT_EQ(blob_top_label_->height(), 1);
    EXPECT_EQ(blob_top_label_->width(), 1);

    for (int iter = 0; iter < 100; ++iter) {
      layer.Forward(blob_bottom_vec_, blob_top_vec_);
      for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i, blob_top_label_->cpu_data()[i]);
      }
      for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 24; ++j) {
          EXPECT_EQ(scale * i, blob_top_data_->cpu_data()[i * 24 + j])
              << "debug: iter " << iter << " i " << i << " j " << j;
        }
      }
    }
  }

  void TestReadCrop() {
    const Dtype scale = 3;
    LayerParameter param;
    Caffe::set_random_seed(1701);

    DataParameter* data_param = param.mutable_data_param();
    data_param->set_batch_size(5);
    data_param->set_source(filename_->c_str());
    data_param->set_backend(backend_);

    TransformationParameter* transform_param =
        param.mutable_transform_param();
    transform_param->set_scale(scale);
    transform_param->set_crop_size(1);

    DataLayer<Dtype> layer(param);
    layer.SetUp(blob_bottom_vec_, blob_top_vec_);
    EXPECT_EQ(blob_top_data_->num(), 5);
    EXPECT_EQ(blob_top_data_->channels(), 2);
    EXPECT_EQ(blob_top_data_->height(), 1);
    EXPECT_EQ(blob_top_data_->width(), 1);
    EXPECT_EQ(blob_top_label_->num(), 5);
    EXPECT_EQ(blob_top_label_->channels(), 1);
    EXPECT_EQ(blob_top_label_->height(), 1);
    EXPECT_EQ(blob_top_label_->width(), 1);

    for (int iter = 0; iter < 2; ++iter) {
      layer.Forward(blob_bottom_vec_, blob_top_vec_);
      for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i, blob_top_label_->cpu_data()[i]);
      }
      int num_with_center_value = 0;
      for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 2; ++j) {
          const Dtype center_value = scale * (j ? 17 : 5);
          num_with_center_value +=
              (center_value == blob_top_data_->cpu_data()[i * 2 + j]);
          // At TEST time, check that we always get center value.
          if (Caffe::phase() == Caffe::TEST) {
            EXPECT_EQ(center_value, this->blob_top_data_->cpu_data()[i * 2 + j])
                << "debug: iter " << iter << " i " << i << " j " << j;
          }
        }
      }
      // At TRAIN time, check that we did not get the center crop all 10 times.
      // (This check fails with probability 1-1/12^10 in a correct
      // implementation, so we call set_random_seed.)
      if (Caffe::phase() == Caffe::TRAIN) {
        EXPECT_LT(num_with_center_value, 10);
      }
    }
  }

  void TestReadCropTrainSequenceSeeded() {
    LayerParameter param;
    DataParameter* data_param = param.mutable_data_param();
    data_param->set_batch_size(5);
    data_param->set_source(filename_->c_str());
    data_param->set_backend(backend_);

    TransformationParameter* transform_param =
        param.mutable_transform_param();
    transform_param->set_crop_size(1);
    transform_param->set_mirror(true);

    // Get crop sequence with Caffe seed 1701.
    Caffe::set_random_seed(seed_);
    vector<vector<Dtype> > crop_sequence;
    {
      DataLayer<Dtype> layer1(param);
      layer1.SetUp(blob_bottom_vec_, blob_top_vec_);
      for (int iter = 0; iter < 2; ++iter) {
        layer1.Forward(blob_bottom_vec_, blob_top_vec_);
        for (int i = 0; i < 5; ++i) {
          EXPECT_EQ(i, blob_top_label_->cpu_data()[i]);
        }
        vector<Dtype> iter_crop_sequence;
        for (int i = 0; i < 5; ++i) {
          for (int j = 0; j < 2; ++j) {
            iter_crop_sequence.push_back(
                blob_top_data_->cpu_data()[i * 2 + j]);
          }
        }
        crop_sequence.push_back(iter_crop_sequence);
      }
    }  // destroy 1st data layer and unlock the db

    // Get crop sequence after reseeding Caffe with 1701.
    // Check that the sequence is the same as the original.
    Caffe::set_random_seed(seed_);
    DataLayer<Dtype> layer2(param);
    layer2.SetUp(blob_bottom_vec_, blob_top_vec_);
    for (int iter = 0; iter < 2; ++iter) {
      layer2.Forward(blob_bottom_vec_, blob_top_vec_);
      for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i, blob_top_label_->cpu_data()[i]);
      }
      for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 2; ++j) {
          EXPECT_EQ(crop_sequence[iter][i * 2 + j],
                    blob_top_data_->cpu_data()[i * 2 + j])
              << "debug: iter " << iter << " i " << i << " j " << j;
        }
      }
    }
  }

  void TestReadCropTrainSequenceUnseeded() {
    LayerParameter param;
    DataParameter* data_param = param.mutable_data_param();
    data_param->set_batch_size(5);
    data_param->set_source(filename_->c_str());
    data_param->set_backend(backend_);

    TransformationParameter* transform_param =
        param.mutable_transform_param();
    transform_param->set_crop_size(1);
    transform_param->set_mirror(true);

    // Get crop sequence with Caffe seed 1701, srand seed 1701.
    Caffe::set_random_seed(seed_);
    srand(seed_);
    vector<vector<Dtype> > crop_sequence;
    {
      DataLayer<Dtype> layer1(param);
      layer1.SetUp(blob_bottom_vec_, blob_top_vec_);
      for (int iter = 0; iter < 2; ++iter) {
        layer1.Forward(blob_bottom_vec_, blob_top_vec_);
        for (int i = 0; i < 5; ++i) {
          EXPECT_EQ(i, blob_top_label_->cpu_data()[i]);
        }
        vector<Dtype> iter_crop_sequence;
        for (int i = 0; i < 5; ++i) {
          for (int j = 0; j < 2; ++j) {
            iter_crop_sequence.push_back(
                blob_top_data_->cpu_data()[i * 2 + j]);
          }
        }
        crop_sequence.push_back(iter_crop_sequence);
      }
    }  // destroy 1st data layer and unlock the db

    // Get crop sequence continuing from previous Caffe RNG state; reseed
    // srand with 1701. Check that the sequence differs from the original.
    srand(seed_);
    DataLayer<Dtype> layer2(param);
    layer2.SetUp(blob_bottom_vec_, blob_top_vec_);
    for (int iter = 0; iter < 2; ++iter) {
      layer2.Forward(blob_bottom_vec_, blob_top_vec_);
      for (int i = 0; i < 5; ++i) {
        EXPECT_EQ(i, blob_top_label_->cpu_data()[i]);
      }
      int num_sequence_matches = 0;
      for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 2; ++j) {
          num_sequence_matches += (crop_sequence[iter][i * 2 + j] ==
                                   blob_top_data_->cpu_data()[i * 2 + j]);
        }
      }
      EXPECT_LT(num_sequence_matches, 10);
    }
  }

  virtual ~DataLayerTest() { delete blob_top_data_; delete blob_top_label_; }

  DataParameter_DB backend_;
  shared_ptr<string> filename_;
  Blob<Dtype>* const blob_top_data_;
  Blob<Dtype>* const blob_top_label_;
  vector<Blob<Dtype>*> blob_bottom_vec_;
  vector<Blob<Dtype>*> blob_top_vec_;
  int seed_;
};

TYPED_TEST_CASE(DataLayerTest, TestDtypesAndDevices);

TYPED_TEST(DataLayerTest, TestReadLevelDB) {
  const bool unique_pixels = false;  // all pixels the same; images different
  this->Fill(unique_pixels, DataParameter_DB_LEVELDB);
  this->TestRead();
}

TYPED_TEST(DataLayerTest, TestReadCropTrainLevelDB) {
  Caffe::set_phase(Caffe::TRAIN);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LEVELDB);
  this->TestReadCrop();
}

// Test that the sequence of random crops is consistent when using
// Caffe::set_random_seed.
TYPED_TEST(DataLayerTest, TestReadCropTrainSequenceSeededLevelDB) {
  Caffe::set_phase(Caffe::TRAIN);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LEVELDB);
  this->TestReadCropTrainSequenceSeeded();
}

// Test that the sequence of random crops differs across iterations when
// Caffe::set_random_seed isn't called (and seeds from srand are ignored).
TYPED_TEST(DataLayerTest, TestReadCropTrainSequenceUnseededLevelDB) {
  Caffe::set_phase(Caffe::TRAIN);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LEVELDB);
  this->TestReadCropTrainSequenceUnseeded();
}

TYPED_TEST(DataLayerTest, TestReadCropTestLevelDB) {
  Caffe::set_phase(Caffe::TEST);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LEVELDB);
  this->TestReadCrop();
}

TYPED_TEST(DataLayerTest, TestReadLMDB) {
  const bool unique_pixels = false;  // all pixels the same; images different
  this->Fill(unique_pixels, DataParameter_DB_LMDB);
  this->TestRead();
}

TYPED_TEST(DataLayerTest, TestReadCropTrainLMDB) {
  Caffe::set_phase(Caffe::TRAIN);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LMDB);
  this->TestReadCrop();
}

// Test that the sequence of random crops is consistent when using
// Caffe::set_random_seed.
TYPED_TEST(DataLayerTest, TestReadCropTrainSequenceSeededLMDB) {
  Caffe::set_phase(Caffe::TRAIN);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LMDB);
  this->TestReadCropTrainSequenceSeeded();
}

// Test that the sequence of random crops differs across iterations when
// Caffe::set_random_seed isn't called (and seeds from srand are ignored).
TYPED_TEST(DataLayerTest, TestReadCropTrainSequenceUnseededLMDB) {
  Caffe::set_phase(Caffe::TRAIN);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LMDB);
  this->TestReadCropTrainSequenceUnseeded();
}

TYPED_TEST(DataLayerTest, TestReadCropTestLMDB) {
  Caffe::set_phase(Caffe::TEST);
  const bool unique_pixels = true;  // all images the same; pixels different
  this->Fill(unique_pixels, DataParameter_DB_LMDB);
  this->TestReadCrop();
}

}  // namespace caffe
