package parhash

import (
	"context"
	hashpb "fs101ex/pkg/gen/hashsvc"
	"fs101ex/pkg/workgroup"
	"github.com/pkg/errors"
	"golang.org/x/sync/semaphore"
	"google.golang.org/grpc"
	"log"
	"net"
	"sync"

	parhashpb "fs101ex/pkg/gen/parhashsvc"
)

type Config struct {
	ListenAddr   string
	BackendAddrs []string
	Concurrency  int
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
type Server struct {
	conf Config

	sem               *semaphore.Weighted
	lock              sync.Mutex
	available_backend int

	stop context.CancelFunc
	l    net.Listener
	wg   sync.WaitGroup
}

func New(conf Config) *Server {
	return &Server{
		conf:              conf,
		sem:               semaphore.NewWeighted(int64(conf.Concurrency)),
		lock:              sync.Mutex{},
		available_backend: 0,
	}
}

func (s *Server) Start(ctx context.Context) (err error) {
	defer func() { err = errors.Wrap(err, "Start()") }()

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
		s.available_backend = (s.available_backend + 1) % len(backends)
		s.lock.Unlock()

		current_data := req.Data[i]

		wg.Go(ctx, func(ctx context.Context) (err error) {
			resp, err := current_backend.Hash(ctx, &hashpb.HashReq{Data: current_data})
			if err != nil {
				return err
			}
			s.lock.Lock()
			hashes[i] = resp.Hash
			s.lock.Unlock()
			return nil
		})
	}

	if err := wg.Wait(); err != nil {
		log.Fatalf("failed to hash: %v", err)
	}

	return nil, nil
}
