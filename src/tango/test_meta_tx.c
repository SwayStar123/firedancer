#include "fd_tango.h"

#if FD_HAS_HOSTED && FD_HAS_AVX

/* This test uses the mcache application region for holding the rx
   flow controls.  We'll use a cache line pair for each reliable
   rx_seq and the very end will hold backpressure counters. */

#define RX_MAX (FD_MCACHE_APP_FOOTPRINT/136UL)

static uchar __attribute__((aligned(FD_FCTL_ALIGN))) shmem[ FD_FCTL_FOOTPRINT( RX_MAX ) ];

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );

# define TEST(c) do if( FD_UNLIKELY( !(c) ) ) { FD_LOG_WARNING(( "FAIL: " #c )); return 1; } while(0)

  char const * _mcache = fd_env_strip_cmdline_cstr ( &argc, &argv, "--mcache", NULL,      NULL );
  char const * _init   = fd_env_strip_cmdline_cstr ( &argc, &argv, "--init",   NULL,      NULL );
  ulong        max     = fd_env_strip_cmdline_ulong( &argc, &argv, "--max",    NULL, ULONG_MAX );
  ulong        rx_cnt  = fd_env_strip_cmdline_ulong( &argc, &argv, "--rx-cnt", NULL,       0UL ); /* Num reliable consumers */

  if( FD_UNLIKELY( !_mcache       ) ) FD_LOG_ERR(( "--mcache not specified" ));
  if( FD_UNLIKELY( rx_cnt>=RX_MAX ) ) FD_LOG_ERR(( "--rx-cnt too large for this unit-test" ));

  fd_rng_t _rng[1]; fd_rng_t * rng = fd_rng_join( fd_rng_new( _rng, 0U, 0UL ) );

  FD_LOG_NOTICE(( "Joining to --mcache %s", _mcache ));
  fd_frag_meta_t * mcache = fd_mcache_join( fd_wksp_map( _mcache ) );
  if( FD_UNLIKELY( !mcache ) ) FD_LOG_ERR(( "join failed" ));

  ulong   depth = fd_mcache_depth    ( mcache );
  ulong * _tx_seq = fd_mcache_seq_laddr( mcache );
  ulong   tx_seq  = _init ? fd_cstr_to_ulong( _init ) : FD_VOLATILE_CONST( *_tx_seq );

  FD_LOG_NOTICE(( "Configuring for --rx-cnt %lu reliable consumers", rx_cnt ));

  fd_fctl_t * fctl = fd_fctl_join( fd_fctl_new( shmem, rx_cnt ) );
  uchar * fctl_top = fd_mcache_app_laddr( mcache );
  uchar * fctl_bot = fctl_top + FD_MCACHE_APP_FOOTPRINT;
  for( ulong rx_idx=0UL; rx_idx<rx_cnt; rx_idx++ ) {
    ulong * rx_lseq  = (ulong *) fctl_top;      fctl_top += 128UL;
    ulong * rx_backp = (ulong *)(fctl_bot-8UL); fctl_bot -=   8UL;
    fd_fctl_cfg_rx_add( fctl, depth, rx_lseq, rx_backp );
    *rx_backp = 0UL;
  }
  fd_fctl_cfg_done( fctl, 0UL, 0UL, 0UL, 0UL );
  
  ulong cr_avail  = 0UL;
  ulong async_rem = 0UL;

  FD_LOG_NOTICE(( "Running --init %lu (%s) --max %lu", tx_seq, _init ? "manual" : "auto", max ));

# define RELOAD (100000000UL)
  ulong iter = 0UL;
  ulong rem  = RELOAD;
  long  tic  = fd_log_wallclock();
  while( iter<max ) {

    /* Do housekeeping */

    if( FD_UNLIKELY( !async_rem ) ) {
      FD_VOLATILE( *_tx_seq ) = tx_seq;
      cr_avail = fd_fctl_tx_cr_update( fctl, cr_avail, tx_seq );
      async_rem = 10000UL; /* FIXME: IDEALLY SHOULD RANDOMIZE THIS */
    }
    async_rem--;

    /* Check if we are backpressured */

    if( FD_UNLIKELY( !cr_avail ) ) continue; /* Backpressuring */
    
    /* We are not backpressured, so send metadata with a test pattern */
    
    ulong sig    =                tx_seq;
    ulong chunk  = (ulong)(uint  )tx_seq;
    ulong sz     = (ulong)(ushort)tx_seq;
    ulong ctl    = (ulong)(ushort)tx_seq;
    ulong tsorig = (ulong)(uint  )tx_seq;
    ulong tspub  = (ulong)(uint  )tx_seq;
    __m256i meta_avx = fd_frag_meta_avx( tx_seq, sig, chunk, sz, ctl, tsorig, tspub );
    _mm256_store_si256( &mcache[ fd_mcache_line_idx( tx_seq, depth ) ].avx, meta_avx );
    tx_seq = fd_seq_inc( tx_seq, 1UL );
    cr_avail--;

    /* Go to the next iteration and, every once in while, log some
       performance metrics */

    iter++;
    rem--;
    if( FD_UNLIKELY( !rem ) ) {
      long  toc  = fd_log_wallclock();
      float mfps = (1e3f*(float)RELOAD) / (float)(toc-tic);
      FD_LOG_NOTICE(( "%10lu: %7.3f Mfrag/s tx", iter, (double)mfps ));
      for( ulong rx_idx=0UL; rx_idx<rx_cnt; rx_idx++ ) {
        ulong * rx_backp = fd_fctl_rx_backp_laddr( fctl, rx_idx );
        FD_LOG_NOTICE(( "backp[%lu] %lu", rx_idx, *rx_backp ));
        *rx_backp = 0UL;
      }
      rem = RELOAD;
      tic = fd_log_wallclock();
    }

  }
# undef RELOAD

  FD_LOG_NOTICE(( "Cleaning up" ));

  FD_VOLATILE( *_tx_seq ) = tx_seq;
  fd_fctl_delete( fd_fctl_leave( fctl ) );
  fd_wksp_unmap( fd_mcache_leave( mcache ) );
  fd_rng_delete( fd_rng_leave( rng ) );

# undef TEST

  FD_LOG_NOTICE(( "pass" ));
  fd_halt();
  return 0;
}

#else

int
main( int     argc,
      char ** argv ) {
  fd_boot( &argc, &argv );
  FD_LOG_WARNING(( "skip: unit test requires FD_HAS_HOSTED and FD_HAS_AVX capabilities" ));
  fd_halt();
  return 0;
}

#endif