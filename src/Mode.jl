immutable Mode_s; end
typealias Mode_ Ptr{Mode_s}
DestroyMode(m::Mode_) = ccall((:DestroyMode, saltlib), Void, (Mode_,), m )

type Mode
	m::Mode_
	psi::PetscVec_
	N::Array{Int64,1}
	Nc::Int64
	h::Array{Cdouble,1}
	function Mode(m::Mode_, g::Geometry)
		md = new(
			m,
			ccall( (:GetVpsi, saltlib), PetscVec_, (Mode_,), m),
			GetN(g),
			GetNc(g),
			GetCellh(g)
		);
		finalizer(md, DestroyMode)
		return md
	end
end

function show(io::IO, mode::Mode)
    print(io, "SALT Mode: ", mode.m, "\n")
	
	print(io, mode.Nc, " electric field components\n");
	print(io, mode.N[1], " x ", mode.N[2], " x ", mode.N[3], " pixels\n");
	h = mode.h;
	N = mode.N;
	Nc = mode.Nc;

	last_element = mode.psi[end];
	omega = mode.psi[end-1] + im * (last_element > 0? 0 : last_element); 
	magnitude = last_element > 0? last_element : 0;

	if( N[2]==1 && N[3] == 1 && Nc == 1)

		x = linspace(0, N[1]*h[1], N[1]);
		psireal = mode.psi[1:N[1]];
		psiimag = mode.psi[N[1]+1:end-2];
		plot(x, psireal, x, psiimag);
		legend(["real", "imag"]);
		title(
			string("Mode: \$\\omega\$ = ", real(omega), " + i(", 
			imag(omega), "), |\$\\Psi\$| = ", magnitude)
		);
		ylabel("\$\\Psi(x)\$");
		xlabel("\$x\$");
	end 
end