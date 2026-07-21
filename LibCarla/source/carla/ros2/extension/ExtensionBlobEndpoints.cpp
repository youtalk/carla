// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.
//
// CycloneDDS raw-CDR blob publisher/subscriber backing the extension host's
// create_publisher / publish / create_subscriber vtable slots. This is the ONE
// extension translation unit that links CycloneDDS: it is globbed into
// carla-ros2-native (where CARLA_ROS2_MIDDLEWARE_CYCLONEDDS and libddsc live),
// NOT carla-server. It reuses the carla_cdr_* raw-CDR passthrough substrate
// (CycloneDDSSertype.h) so a pre-serialized CDR buffer produced by the
// out-of-tree extension is put on the wire verbatim and delivered back verbatim
// — CARLA core never needs the extension's message vocabulary.

#include "carla/ros2/extension/ExtensionBlobEndpoints.h"
#include "carla/Logging.h"

#if defined(CARLA_ROS2_MIDDLEWARE_CYCLONEDDS)

#include "carla/ros2/middleware/cyclonedds/CycloneDDSSertype.h"
#include "carla/ros2/middleware/Middleware.h"
// ActiveMiddleware.h (NOT MiddlewareFactory.h): the DDS-free bridge exposes the
// process middleware selection through Middleware.h only. Including
// MiddlewareFactory.h here would pull the FastDDS vendor headers, whose
// ParameterTypes.hpp PID_* enum collides with CycloneDDS's q_protocol.h PID_*
// macros already in this raw-DDS TU (via CycloneDDSSertype.h).
#include "carla/ros2/middleware/ActiveMiddleware.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace carla {
namespace ros2 {

namespace {

struct BlobWriter {
  dds_entity_t writer;
  struct ddsi_sertype* sertype;
};

struct BlobReader {
  dds_entity_t reader;
  CarlaRos2SubCallback cb;
  void* user;
};

std::mutex g_mtx;
std::unordered_map<uint64_t, BlobWriter> g_writers;
std::unordered_map<uint64_t, BlobReader*> g_readers;
uint64_t g_next = 1u;  // 0 is the ABI's "invalid handle" sentinel

// Runtime CycloneDDS gate. The compile-time #else branch below is dead in the
// shipped .so (which always defines the macro), so it is NOT the guarantee: on a
// process launched with --rmw=fastdds / --rmw=zenoh these functions would
// otherwise silently spin up an isolated CycloneDDS participant (via
// carla_cdr_get_participant) and hand out valid handles that talk to nobody the
// rest of CARLA publishes to. Refuse loudly instead — v1 blob pub/sub is
// CycloneDDS-only. GetActiveMiddleware() reads the same process-wide selection
// SetActiveMiddleware() latched from --rmw at startup.
bool CycloneDdsActive() {
  return GetActiveMiddleware() == Middleware::CycloneDDS;
}

// Map an extension-supplied ROS topic name to its DDS wire name. ROS 2 <-> DDS
// maps message topic "/<x>" <-> DDS "rt/<x>", so a raw CycloneDDS endpoint is
// invisible to ROS 2 nodes without the "rt" prefix. Mirrors the verbatim-
// override branch of ROS2::BuildBaseTopicName (ROS2.cpp) exactly. An empty
// topic is passed through unchanged so carla_cdr_create_topic() fails loudly
// rather than creating a bogus "rt/" topic.
std::string BlobDdsTopicName(const char* topic) {
  if (topic == nullptr || topic[0] == '\0') {
    return std::string{};
  }
  std::string t(topic);
  return t.front() == '/' ? "rt" + t : "rt/" + t;
}

dds_qos_t* MakeQos(const CarlaRos2Qos* q) {
  dds_qos_t* qos = dds_create_qos();
  // ABI: reliability 0 = reliable, 1 = best_effort; durability 0 = volatile,
  // 1 = transient_local; history_depth 0 clamps to 1.
  dds_qset_reliability(
      qos, q->reliability ? DDS_RELIABILITY_BEST_EFFORT : DDS_RELIABILITY_RELIABLE,
      DDS_SECS(1));
  dds_qset_durability(
      qos, q->durability ? DDS_DURABILITY_TRANSIENT_LOCAL : DDS_DURABILITY_VOLATILE);
  dds_qset_history(qos, DDS_HISTORY_KEEP_LAST,
                   q->history_depth ? static_cast<int32_t>(q->history_depth) : 1);
  return qos;
}

// data-available listener: drains every available sample and delivers its raw
// CDR bytes to the extension callback. Uses dds_takecdr() (NOT dds_take): the
// carla_cdr sertype deliberately fails to_sample() ("use dds_takecdr instead"),
// so the typed take path returns nothing — this mirrors
// CycloneDDSSubscriberMiddleware::carla_on_data_available.
void ReaderListener(dds_entity_t rd, void* arg) {
  auto* br = static_cast<BlobReader*>(arg);
  struct ddsi_serdata* sd = nullptr;
  dds_sample_info_t info;
  while (dds_takecdr(rd, &sd, 1u, &info, DDS_ANY_STATE) > 0 && sd != nullptr) {
    if (info.valid_data) {
      br->cb(br->user, carla_cdr_data(sd), carla_cdr_size(sd));
    }
    ddsi_serdata_unref(sd);
    sd = nullptr;
  }
}

}  // namespace

CarlaRos2PubHandle BlobCreatePublisher(const char* topic, const char* type_name,
                                       const char* /*type_hash*/,
                                       const CarlaRos2Qos* qos) {
  if (!CycloneDdsActive()) {
    log_error("ROS2 extension blob publisher requires CycloneDDS "
              "(--rmw=cyclonedds); FastDDS/Zenoh unsupported in v1 — refusing",
              topic ? topic : "(null)");
    return 0u;
  }
  // type_hash (RIHS01 form) is accepted but deliberately NOT placed on the wire:
  // ROS 2 Humble ships no RIHS01 type-hash discovery machinery, so endpoint
  // matching is by topic name + type name + QoS. Recording it in
  // USER_DATA would be inert for Humble interop and is omitted for v1.
  const std::string dds_topic = BlobDdsTopicName(topic);
  dds_entity_t p = carla_cdr_get_participant();
  struct ddsi_sertype* st = nullptr;
  dds_entity_t t = carla_cdr_create_topic(p, dds_topic.c_str(), type_name, &st);
  if (t < 0) {
    log_error("blob publisher: create_topic failed for", dds_topic);
    return 0u;
  }
  dds_qos_t* q = MakeQos(qos);
  dds_entity_t w = dds_create_writer(p, t, q, nullptr);
  dds_delete_qos(q);
  if (w < 0) {
    log_error("blob publisher: create_writer failed for", dds_topic);
    return 0u;
  }
  std::lock_guard<std::mutex> lk(g_mtx);
  uint64_t h = g_next++;
  g_writers[h] = BlobWriter{w, st};
  return h;
}

int BlobPublish(CarlaRos2PubHandle h, const uint8_t* cdr, size_t len) {
  dds_entity_t writer;
  struct ddsi_sertype* sertype;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    auto it = g_writers.find(h);
    if (it == g_writers.end()) {
      return -1;
    }
    writer = it->second.writer;
    sertype = it->second.sertype;
  }
  // Wrap + write outside the registry lock: dds_writecdr() can block on QoS
  // flow control, and holding g_mtx across it would serialize every publisher.
  struct ddsi_serdata* sd =
      carla_cdr_wrap(sertype, cdr, static_cast<uint32_t>(len));
  if (!sd) {
    return -1;
  }
  // dds_writecdr() consumes exactly 1 reference from sd ONLY on success; on
  // failure the reference is NOT consumed, so we must unref it manually or leak
  // one carla_cdr_serdata per failed write (mirrors
  // CycloneDDSPublisherMiddleware::Publish). Also normalize the raw negative
  // dds_return_t to the ABI's documented -1.
  dds_return_t rc = dds_writecdr(writer, sd);
  if (rc != DDS_RETCODE_OK) {
    ddsi_serdata_unref(sd);
    return -1;
  }
  return 0;
}

CarlaRos2SubHandle BlobCreateSubscriber(const char* topic, const char* type_name,
                                        const char* /*type_hash*/,
                                        const CarlaRos2Qos* qos,
                                        CarlaRos2SubCallback cb, void* user) {
  if (!CycloneDdsActive()) {
    log_error("ROS2 extension blob subscriber requires CycloneDDS "
              "(--rmw=cyclonedds); FastDDS/Zenoh unsupported in v1 — refusing",
              topic ? topic : "(null)");
    return 0u;
  }
  // type_hash intentionally not placed on the wire — see BlobCreatePublisher.
  if (cb == nullptr) {
    log_error("blob subscriber: null callback for", topic ? topic : "(null)");
    return 0u;
  }
  const std::string dds_topic = BlobDdsTopicName(topic);
  dds_entity_t p = carla_cdr_get_participant();
  // The registered sertype is owned by the participant and used only by the
  // reader's own take path; the BlobReader does not need to retain it.
  struct ddsi_sertype* st = nullptr;
  dds_entity_t t = carla_cdr_create_topic(p, dds_topic.c_str(), type_name, &st);
  if (t < 0) {
    log_error("blob subscriber: create_topic failed for", dds_topic);
    return 0u;
  }
  auto* br = new BlobReader{DDS_ENTITY_NIL, cb, user};
  // The listener's creation arg (br) is delivered to ReaderListener as its
  // void* arg (dds_lset_data_available, matching CycloneDDSSubscriberMiddleware).
  dds_listener_t* l = dds_create_listener(br);
  dds_lset_data_available(l, ReaderListener);
  dds_qos_t* q = MakeQos(qos);
  br->reader = dds_create_reader(p, t, q, l);
  dds_delete_qos(q);
  dds_delete_listener(l);
  if (br->reader < 0) {
    log_error("blob subscriber: create_reader failed for", dds_topic);
    delete br;
    return 0u;
  }
  std::lock_guard<std::mutex> lk(g_mtx);
  uint64_t h = g_next++;
  g_readers[h] = br;
  return h;
}

void BlobTeardownAll() {
  // Move the registries out under the lock, then delete the DDS entities with
  // the lock RELEASED. dds_set_listener(nullptr)/dds_delete() block until any
  // in-flight ReaderListener returns, and that callback (br->cb) may re-enter
  // BlobPublish — which takes g_mtx — so deleting under g_mtx would deadlock.
  std::unordered_map<uint64_t, BlobReader*> readers;
  std::unordered_map<uint64_t, BlobWriter> writers;
  {
    std::lock_guard<std::mutex> lk(g_mtx);
    readers.swap(g_readers);
    writers.swap(g_writers);
  }
  // Readers FIRST: detach the listener (waits for in-flight callbacks to
  // finish, so cb/user are never touched again), then delete the reader, then
  // free the BlobReader — closing the shutdown-window use-after-free.
  for (auto& kv : readers) {
    BlobReader* br = kv.second;
    if (br != nullptr) {
      if (br->reader > 0) {
        dds_set_listener(br->reader, nullptr);
        dds_delete(br->reader);
      }
      delete br;
    }
  }
  for (auto& kv : writers) {
    if (kv.second.writer > 0) {
      dds_delete(kv.second.writer);
    }
  }
}

}  // namespace ros2
}  // namespace carla

#else  // not CycloneDDS — v1 supports CycloneDDS only.

namespace carla {
namespace ros2 {

CarlaRos2PubHandle BlobCreatePublisher(const char*, const char*, const char*,
                                       const CarlaRos2Qos*) {
  carla::log_error(
      "ROS2 extension blob pub/sub requires CycloneDDS "
      "(--rmw=cyclonedds); FastDDS/Zenoh unsupported in v1");
  return 0u;
}

int BlobPublish(CarlaRos2PubHandle, const uint8_t*, size_t) { return -1; }

CarlaRos2SubHandle BlobCreateSubscriber(const char*, const char*, const char*,
                                        const CarlaRos2Qos*, CarlaRos2SubCallback,
                                        void*) {
  carla::log_error(
      "ROS2 extension blob pub/sub requires CycloneDDS "
      "(--rmw=cyclonedds); FastDDS/Zenoh unsupported in v1");
  return 0u;
}

void BlobTeardownAll() {}

}  // namespace ros2
}  // namespace carla

#endif  // CARLA_ROS2_MIDDLEWARE_CYCLONEDDS
