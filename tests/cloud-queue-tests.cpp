#include "cloud/upload-queue.hpp"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cassert>
#include <fstream>

using namespace ReplayBufferPro::Cloud;

int main(int argc, char **argv)
{
  QCoreApplication application(argc, argv);
  (void)application;
  QTemporaryDir temporary;
  assert(temporary.isValid());
  const auto clip = std::filesystem::path(temporary.path().toStdString()) / "clip.mp4";
  std::ofstream(clip) << "video";
  const auto queuePath = std::filesystem::path(temporary.path().toStdString()) / "queue.json";

  UploadQueue queue(queuePath);
  UploadJob job;
  job.id = "job-1";
  job.path = clip;
  job.remoteFolder = "OBS Clips/2026-09-14";
  job.status = UploadStatus::Uploading;
  job.retryCount = 2;
  queue.add(job);
  assert(queue.save());

  UploadQueue recovered(queuePath);
  assert(recovered.load());
  assert(recovered.jobs().size() == 1);
  assert(recovered.jobs().front().status == UploadStatus::Pending);
  assert(recovered.jobs().front().retryCount == 2);
  assert(recovered.nextReady(0).has_value());
  assert(retryDelaySeconds(1) == 5);
  assert(retryDelaySeconds(2) == 15);
  assert(retryDelaySeconds(99) == 300);

  std::filesystem::remove(clip);
  UploadQueue missing(queuePath);
  assert(missing.load());
  assert(missing.jobs().empty());
  return 0;
}
