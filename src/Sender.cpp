#include "Sender.hpp"

#include <iostream>
#include <chrono>

Sender::Sender(int sender_port, int receiver_port, int router_port)
{
  this->sender_port = sender_port;
  this->receiver_port = receiver_port;
  this->router_port = router_port;
}

void Sender::start(string file_location)
{
  setup_socket();

  auto segments = slice_file(file_location);
  vector<bool> segments_ack(segments.size(), false);

  cout << "Segments array size " << segments.size() << endl;
  int window_start = 0;

  auto begin = std::chrono::steady_clock::now();
  // send first window
  send_bulk(segments, window_start);

  while (window_start != segments.size())
  {
    if (has_segment_expired(segments[window_start]))
    {
      cout << "Segment with sequence " << segments[window_start].get_seq_num() << " expired" << endl;
      send_bulk(segments, window_start);
    }
    else
    {
      auto ack_segment = receive_ack();
      if (ack_segment == NULL)
        continue;

      cout << "ack segment for segment " << ack_segment->get_acknowlegment() << " received" << endl;
      if (ack_segment->get_acknowlegment() == segments[window_start].get_seq_num())
      {
        segments_ack[window_start] = true;
        auto updated_window_start = update_window_start(segments_ack, window_start);

        send_bulk(segments, window_start + WINDOW_SIZE, updated_window_start - window_start);
        window_start = updated_window_start;
      }
      else
      {
        auto furthure_ack_index = get_index(window_start, ack_segment->get_acknowlegment());
        cout << "segments_ack[" << furthure_ack_index << "] = true. window: " << window_start << " ack: " << ack_segment->get_acknowlegment() << endl;
        segments_ack[furthure_ack_index] = true;
      }
      delete ack_segment;
    }
  }
  auto end = std::chrono::steady_clock::now();
  cout << "file transmission time = " << chrono::duration_cast<chrono::milliseconds>(end - begin).count() << "[ms]" << endl;
}

int Sender::update_window_start(vector<bool> &segments_ack, int window_start)
{
  auto prev_window = window_start;
  while (segments_ack[window_start])
    window_start++;
  cout << "window is now: " << window_start << " delta: " << window_start - prev_window << endl;
  return window_start;
}

void Sender::setup_socket()
{
  // Creating socket file descriptor
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    cerr << "socket creation failed" << endl;
    exit(EXIT_FAILURE);
  }

  memset(&router_addr, 0, sizeof(router_addr));

  // Filling server information
  router_addr.sin_family = AF_INET;
  router_addr.sin_port = htons(this->router_port);
  router_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
}

vector<Segment> Sender::slice_file(string file_location)
{
  vector<Segment> result;
  char buffer[PAYLOAD_SIZE];

  auto file_segment = Segment(file_location.c_str());
  file_segment.set_flag(FILE_NAME);
  result.push_back(file_segment);

  std::ifstream infile(file_location);
  while (infile.read(buffer, PAYLOAD_SIZE))
  {
    result.push_back(Segment(buffer));

    memset(buffer, 0, strlen(buffer));
  }
  result.push_back(Segment(buffer));

  return result;
}

void Sender::send_bulk(vector<Segment> &segments, int start_from, int count)
{
  for (int i = 0; i < count; i++)
  {
    if (i + start_from >= segments.size())
      break;
    send_segment(segments[i + start_from], i + start_from);
  }
}

void Sender::send_segment(Segment &segment, int segment_index)
{
  segment.set_ports(this->sender_port, this->receiver_port);
  segment.set_seq_num(get_seq_num(segment_index));
  // send with socket
  char buffer[SEGMENT_SIZE];

  segment.serialize(buffer);

  // age bug dasht moshkele ine
  sendto(sockfd, buffer, strlen(buffer), MSG_CONFIRM,
         (const struct sockaddr *)&router_addr, sizeof(router_addr));
  segment.set_sent_time(time(NULL));
  cout << "Segment with seq_num " << segment.get_seq_num() << " sent" << endl;
}

int Sender::get_seq_num(int segment_index)
{
  return segment_index % (MAX_SEQ);
}

int Sender::get_index(int start_window, int seq_num)
{
  int window_start_seq_num = get_seq_num(start_window);
  int delta = seq_num - window_start_seq_num;

  if (delta < 0)
    delta += MAX_SEQ;

  if (delta > 10)
    delta -= MAX_SEQ;

  return start_window + delta;
}

bool Sender::has_segment_expired(Segment segment)
{
  auto seg_time = segment.get_sent_time();

  bool answer = time(NULL) - seg_time > TIME_OUT_LENGTH;

  if (answer)
    cout << endl;

  return answer;
}

Segment *Sender::receive_ack()
{
  char buffer[SEGMENT_SIZE];
  fd_set read_fds;
  int nbytes;
  socklen_t router_addr_len = sizeof(router_addr);

  FD_ZERO(&read_fds);
  FD_SET(sockfd, &read_fds);

  struct timeval tv;
  tv.tv_sec = 0;
  tv.tv_usec = 0;

  int n = select(sockfd + 1, &read_fds, 0, 0, &tv);

  if (n < 0)
  {
    cerr << "ERROR Server : select()\n"
         << endl;
    close(sockfd);
    exit(1);
  }

  if (FD_ISSET(sockfd, &read_fds))
  {
    nbytes = recvfrom(sockfd, buffer, SEGMENT_SIZE, 0, (struct sockaddr *)&router_addr, &router_addr_len);
    if (nbytes < 0)
    {
      cerr << "ERROR in recvfrom()" << endl;
      close(sockfd);
      exit(1);
    }

    auto return_segment = new Segment();
    return_segment->deserialize(buffer);

    return return_segment;
  }
  FD_CLR(sockfd, &read_fds);
  return nullptr;
}

int main(int argc, char *argv[])
{
  auto sender_port = stoi(argv[1]);
  auto receiver_port = stoi(argv[2]);
  auto router_port = stoi(argv[3]);
  auto file_location = string(argv[4]);
  Sender sender(sender_port, receiver_port, router_port);

  sender.start(file_location);
}