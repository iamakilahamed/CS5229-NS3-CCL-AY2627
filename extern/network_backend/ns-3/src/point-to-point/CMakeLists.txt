set(mpi_sources)
set(mpi_headers)
set(mpi_libraries)

if(${ENABLE_MPI})
  set(mpi_sources
      model/point-to-point-remote-channel.cc
  )
  set(mpi_headers
      model/point-to-point-remote-channel.h
  )
  set(mpi_libraries
      ${libmpi}
      ${MPI_CXX_LIBRARIES}
  )
endif()

build_lib(
  LIBNAME point-to-point
  SOURCE_FILES
    ${mpi_sources}
    helper/point-to-point-helper.cc
    helper/qbb-helper.cc
    helper/selective-packet-queue.cc
    model/point-to-point-channel.cc
    model/point-to-point-net-device.cc
    model/point-to-point-remote-channel.cc
    model/pause-header.cc
    model/cn-header.cc
    model/ppp-header.cc
    model/qbb-net-device.cc
    model/qbb-header.cc
    model/qbb-channel.cc
    model/qbb-remote-channel.cc
    model/rdma-queue-pair.cc
    model/rdma-driver.cc
    model/rdma-hw.cc
    model/switch-node.cc
    model/switch-mmu.cc
		# model/load-balancing-conga.cc
    # model/load-balancing-letflow.cc
    # model/load-balancing-conweave.cc
    # model/conweave-voq.cc
    model/load-balancing-placeholder.cc
    model/load-balancing-ecmp.cc
    model/load-balancing-customized.cc
    model/flow-stat-tag.cc
    model/pint.cc
    model/settings.cc
  HEADER_FILES
    ${mpi_headers}
    helper/point-to-point-helper.h
    helper/qbb-helper.h
    helper/sim-setting.h
    helper/selective-packet-queue.h
    model/point-to-point-channel.h
    model/point-to-point-net-device.h
    model/point-to-point-remote-channel.h
    model/pause-header.h
    model/cn-header.h
    model/ppp-header.h
    model/qbb-net-device.h
    model/qbb-header.h
    model/qbb-channel.h
    model/qbb-remote-channel.h
    model/rdma-queue-pair.h
    model/rdma-driver.h
    model/rdma-hw.h
    model/switch-node.h
    model/switch-mmu.h
    # model/load-balancing-conga.h
    # model/load-balancing-letflow.h
    # model/load-balancing-conweave.h
    # model/conweave-voq.h
    model/load-balancing-placeholder.h
    model/load-balancing-ecmp.h
    model/load-balancing-customized.h
    model/flow-stat-tag.h
    model/pint.h
    model/settings.h
    model/trace-format.h
  LIBRARIES_TO_LINK ${libnetwork}
                    ${mpi_libraries}
                    ${libinternet}
  TEST_SOURCES test/point-to-point-test.cc
)
