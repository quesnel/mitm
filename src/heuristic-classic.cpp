/* Copyright (C) 2015 INRA
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <mitm/mitm.hpp>
#include <iterator>
#include <Eigen/Core>
#include "cstream.hpp"
#include "internal.hpp"
#include "assert.hpp"

namespace mitm {
namespace classic {

struct constraint
{
    std::vector<mitm::index> I;
    std::vector<std::tuple<mitm::real, mitm::index>> r;
    mitm::index k;
    mitm::index n;
    mitm::index bk;

    constraint() = default;

    constraint(mitm::index k_, mitm::index n_, mitm::index bk_,
               const Eigen::MatrixXi& a)
        : k(k_)
        , bk(bk_)
    {
        for (mitm::index i = 0; i != n_; ++i) {
            if (a(k, i) != 0) {
                I.emplace_back(i);
                r.emplace_back(0, i);
            }
        }
    }

    void update(const Eigen::MatrixXi& A, const Eigen::RowVectorXf& c,
                Eigen::MatrixXf& P, Eigen::VectorXf& pi, Eigen::VectorXi& x,
                mitm::real kappa, mitm::real l, mitm::real theta)
    {
        P.row(k) *= theta;

        for (mitm::index i = 0; i != static_cast<mitm::index>(I.size()); ++i) {
            mitm::real sum_a_hi_pi_h = 0;
            mitm::real sum_a_hi_p_hi = 0;
            for (mitm::index h = 0, endh = A.rows(); h != endh; ++h) {
                if (A(h, i)) {
                    sum_a_hi_pi_h += A(h, I[i]) * pi(h);
                    sum_a_hi_p_hi += A(h, I[i]) * P(h, I[i]);
                }
            }

            r[i] = std::make_tuple(c(I[i]) - sum_a_hi_pi_h - sum_a_hi_p_hi,
                                   I[i]);
        }

        std::sort(r.begin(), r.end(),
                  [](const std::tuple<mitm::real, mitm::index>& lhs,
                     const std::tuple<mitm::real, mitm::index>& rhs)
                  {
                      return std::get<0>(lhs) < std::get<0>(rhs);
                  });

        pi(k) += (std::get<0>(r[bk - 1]) + std::get<0>(r[bk])) / 2.0;
        const mitm::real delta = ((kappa / (1 - kappa)) *
                                  (std::get<0>(r[bk - 1]) - std::get<0>(r[bk]))
                             + l);

        for (mitm::index j = 0; j < bk; ++j) {
            x(std::get<1>(r[j])) = 1;
            P(k, std::get<1>(r[j])) -= +delta;
        }

        for (mitm::index j = bk; j != static_cast<mitm::index>(I.size()); ++j) {
            x(std::get<1>(r[j])) = 0;
            P(k, std::get<1>(r[j])) -= -delta;
        }
    }

    friend std::ostream&
        operator<<(std::ostream& os, const constraint& c)
        {
            os << "k: " << c.k << " n: " << c.n << " bk: " << c.bk << '\n';
            os << "I: ";
            std::copy(c.I.cbegin(), c.I.cend(),
                      std::ostream_iterator<mitm::index>(os, " "));

            os << "\nr: ";
            for (const auto& t : c.r)
                os << '[' << std::get<0>(t) << ',' << std::get<1>(t) << "] ";

            return os << '\n';
        }

    std::size_t size() const
    {
        return I.size() * sizeof(mitm::index) +
            r.size() * sizeof(std::tuple<mitm::real, mitm::index>) +
            3 * sizeof(mitm::index);
    }
};

struct wedelin_heuristic
{
    std::size_t size() const
    {
        std::size_t ret = std::accumulate(
            constraints.cbegin(),
            constraints.cend(),
            0.0, [](std::size_t init, const constraint& c)
            {
                return init + c.size();
            });

        ret += A.size() * sizeof(int) +
            b.size() * sizeof(int) +
            c.size() * sizeof(mitm::real) +
            x.size() * sizeof(int) +
            P.size() * sizeof(mitm::real) +
            pi.size() * sizeof(mitm::real) +
            2 * sizeof(index) +
            3 * sizeof(mitm::real);

        return ret;
    }

    std::vector <constraint> constraints;
    Eigen::MatrixXi A;
    Eigen::VectorXi b;
    Eigen::RowVectorXf c;
    Eigen::VectorXi x;
    Eigen::MatrixXf P;
    Eigen::VectorXf pi;
    index m;
    index n;
    mitm::real kappa;
    mitm::real l;
    mitm::real theta;

    wedelin_heuristic(const SimpleState &s, mitm::index m_, mitm::index n_,
                      mitm::real k_, mitm::real l_, mitm::real theta_)
        : constraints(m_)
        , A(Eigen::MatrixXi::Zero(m_, n_))
        , b(Eigen::VectorXi::Zero(m_))
        , c(Eigen::RowVectorXf::Zero(n_))
        , x(Eigen::VectorXi::Zero(n_))
        , P(Eigen::MatrixXf::Zero(m_, n_))
        , pi(Eigen::VectorXf::Zero(m_))
        , m(m_)
        , n(n_)
        , kappa(k_)
        , l(l_)
        , theta(theta_)
    {
        {
            mitm::index longi = 0;
            for (mitm::index i = 0; i != m; ++i)
                for (mitm::index j = 0; j != n; ++j, ++longi)
                    A(i, j) = s.a[longi];
        }

        for (mitm::index i = 0; i != m; ++i) {
            b(i) = s.b[i];
        }

        for (mitm::index j = 0; j != n; ++j) {
            c(j) = s.c[j];
            x(j) = c(j) <= 0;
        }

        // TODO: intialize parameters delta, kappa.
        Ensures(kappa >= 0 && kappa < 1, "kappa must be [0..1[");
        Ensures(l >= 0, "l must be [0..+oo[");
        Ensures(theta >= 0 && theta <= 1, "theta must be [0..1]");

        constraints.clear();
        for (mitm::index i = 0; i != m; ++i)
            constraints.emplace_back(i, n, b(i), A);
    }

    inline bool
    is_constraint_need_update(mitm::index k) const
    {
        // TODO: Found a Eigen API (sum(A.row(k) * x(k)) == b(k).
        int sum = 0;
        for (mitm::index i = 0; i != n; ++i)
            sum += A(k, i) * x(i);

        return sum != b(k);
    }

    bool next()
    {
        for (mitm::index k = 0; k != m; ++k)
            if (is_constraint_need_update(k))
                constraints[k].update(A, c, P, pi, x, kappa, l, theta);

        if (is_ax_equal_b())
            return true;

        // TODO: adjust parameters kappa, delta, theta

        return false;
    }

    friend std::ostream&
        operator<<(std::ostream &os, const wedelin_heuristic &wh)
        {
            return os << "A:\n" << wh.A << '\n'
                      << "P:\n" << wh.P << '\n'
                      << "pi: " << wh.pi.transpose() << '\n'
                      << "b: " << wh.b.transpose() << '\n'
                      << "c: " << wh.c << '\n'
                      << "X: " << wh.x.transpose() << '\n'
                      << "(Ax): "<< (wh.A * wh.x).transpose() << '\n';
        }

private:
    bool is_ax_equal_b() const
    {
        return (A * x) == b;
    }
};

}

mitm::result
heuristic_algorithm_default(const SimpleState &s, index limit,
                            mitm::real kappa, mitm::real delta, mitm::real theta)
{
    Expects(s.b.size() > 0 && s.c.size() > 0 &&
            s.a.size() == s.b.size() * s.c.size(),
            "heuristic_algorithm_default: state not initialized");

    mitm::classic::wedelin_heuristic wh(
        s,
        static_cast<mitm::index>(s.b.size()),
        static_cast<mitm::index>(s.c.size()),
        kappa, delta, theta);

    mitm::out() << "heuristic_algorithm_default start:\n"
                << "constraints: " << mitm::out().yellow() << s.b.size()
                << mitm::out().reset()
                << " variables: " << mitm::out().yellow() << s.c.size()
                << mitm::out().reset()
                << "\nlimit: " << mitm::out().yellow()
                << limit << mitm::out().reset()
                << " kappa: " << mitm::out().yellow()
                << kappa << mitm::out().reset()
                << " delta: " << mitm::out().yellow()
                << delta << mitm::out().reset()
                << " theta: " << mitm::out().yellow()
                << theta << mitm::out().reset()
                << "\n"
                << "Memory allocated: " << mitm::out().yellow();

    if (wh.size() < 1024)
        mitm::out() << wh.size() << " B" << mitm::out().reset() << "\n";
    else if (wh.size() < 1024 * 1024)
        mitm::out() << (wh.size() / 1024.0) << " KB" << mitm::out().reset() << "\n";
    else
        mitm::out() << (wh.size() / (1024.0 * 1024.0)) << " MB"
            << mitm::out().reset() << "\n";

    for (mitm::index it = 0; it != limit; ++it) {
        if (wh.next()) {
            mitm::result ret;

            ret.x.resize(wh.x.size());

            for (mitm::index j = 0;
                 j != static_cast<mitm::index>(s.c.size()); ++j)
                ret.x[j] = wh.x(j);

            ret.loop = it;
            return ret;
        }
    }

    throw std::runtime_error("no solution founded");
}

}
