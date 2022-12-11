package parhash

import (
	"context"
	"github.com/pkg/errors"
	"github.com/prometheus/client_golang/prometheus"
	"golang.org/x/sync/semaphore"
	"time"

	hashpb "fs101ex/pkg/gen/hashsvc"
	parhashpb "fs101ex/pkg/gen/parhashsvc"
	"fs101ex/pkg/workgroup"
	"google.golang.org/grpc"
	"log"
	"net"
	"sync"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int

	Prom prometheus.Registerer
}

// Implement a server that responds to ParallelHash()
// as declared in /proto/parhash.proto.
//
// The implementation of ParallelHash() must not hash the content
// of buffers on its own. Instead, it must send buffers to backends
// to compute hashes. Buffers must be fanned out to backends in the
// round-robin fashion.
//
// For example, suppose that 2 backends are configured and ParallelHash()
// is called to compute hashes of 5 buffers. In this case it may assign
// buffers to backends in this way:
//
//	backend 0: buffers 0, 2, and 4,
//	backend 1: buffers 1 and 3.
//
// Requests to hash individual buffers must be issued concurrently.
// Goroutines that issue them must run within /pkg/workgroup/Wg. The
// concurrency within workgroups must be limited by Server.sem.
//
// WARNING: requests to ParallelHash() may be concurrent, too.
// Make sure that the round-robin fanout works in that case too,
// and evenly distributes the load across backends.
//
// The server must report the following performance counters to Prometheus:
//
//  1. nr_nr_requests: a counter that is incremented every time a call
//     is made to ParallelHash(),
//
//  2. subquery_durations: a histogram that tracks durations of calls
//     to backends.
//     It must have a label `backend`.
//     Each subquery_durations{backend=backend_addr} must be a histogram
//     with 24 exponentially growing buckets ranging from 0.1ms to 10s.
//
// Both performance counters must be placed to Prometheus namespace "parhash".

type Server struct {
	conf Config

	sem                *semaphore.Weighted
	lock               sync.Mutex
	available_backend  int
	nr_nr_requests     prometheus.Counter
	subquery_durations *prometheus.HistogramVec

	stop context.CancelFunc
	l    net.Listener
	wg   sync.WaitGroup
}

func New(conf Config) *Server {
	var counter_opts prometheus.CounterOpts
	counter_opts.Namespace = "parhash"
	counter_opts.Name = "nr_requests"

	var histogram_opts prometheus.HistogramOpts
	histogram_opts.Namespace = "parhash"
	histogram_opts.Name = "subquery_durations"
	histogram_opts.Buckets = prometheus.ExponentialBuckets(0.0001, 1.6156, 24)

	return &Server{
		conf:               conf,
		sem:                semaphore.NewWeighted(int64(conf.Concurrency)),
		lock:               sync.Mutex{},
		available_backend:  0,
		nr_nr_requests:     prometheus.NewCounter(counter_opts),
		subquery_durations: prometheus.NewHistogramVec(histogram_opts, []string{"backend"}),
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

	s.conf.Prom.MustRegister(s.nr_nr_requests)
	s.conf.Prom.MustRegister(s.subquery_durations)

	ctx, s.stop = context.WithCancel(ctx)

	s.l, err = net.Listen("tcp", s.conf.ListenAddr)
	if err != nil {
		return err
	}

	srv := grpc.NewServer()
	parhashpb.RegisterParallelHashSvcServer(srv, s)

	s.wg.Add(2)
	go func() {
		defer s.wg.Done()

		srv.Serve(s.l)
	}()
	go func() {
		defer s.wg.Done()

		<-ctx.Done()
		s.l.Close()
	}()

	return nil
}

func (s *Server) ListenAddr() string {
	return s.l.Addr().String()
}

func (s *Server) Stop() {
	s.stop()
	s.wg.Wait()
}

func (s *Server) ParallelHash(
	ctx context.Context, req *parhashpb.ParHashReq) (resp *parhashpb.ParHashResp, err error) {
	s.nr_nr_requests.Inc()
	var backends []hashpb.HashSvcClient
	for _, backend_addr := range s.conf.BackendAddrs {
		conn, err := grpc.Dial(backend_addr,
			grpc.WithInsecure(), /* allow non-TLS connections */
		)
		if err != nil {
			log.Fatalf("failed to connect to %q: %v", backend_addr, err)
		}
		defer conn.Close()

		client := hashpb.NewHashSvcClient(conn)
		backends = append(backends, client)
	}

	var (
		wg     = workgroup.New(workgroup.Config{Sem: s.sem})
		hashes = make([][]byte, len(req.Data))
	)
	for i := range hashes {
		s.lock.Lock()
		current_backend := backends[s.available_backend]
		current_backend_addr := s.conf.BackendAddrs[s.available_backend]
		s.available_backend = (s.available_backend + 1) % len(backends)
		s.lock.Unlock()

		current_data := req.Data[i]
		current_index := i

		wg.Go(ctx, func(ctx context.Context) (err error) {
			start := time.Now()
			resp, err := current_backend.Hash(ctx, &hashpb.HashReq{Data: current_data})
			duration := time.Since(start)
			if err != nil {
				return err
			}
			s.subquery_durations.With(prometheus.Labels{"backend": current_backend_addr}).Observe(duration.Seconds())
			s.lock.Lock()
			hashes[current_index] = resp.Hash
			s.lock.Unlock()
			return nil
		})
	}

	if err := wg.Wait(); err != nil {
		log.Fatalf("failed to hash: %v", err)
	}

	return &parhashpb.ParHashResp{Hashes: hashes}, nil
}
