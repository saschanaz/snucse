// Description
// ========
// 본 프로그램은 크게 아래 세 단계를 거쳐 실행된다.
// 만약 아래의 세 단계중 하나라도 문제가 생기면, `ERROR`만을 출력하고 그 외에 어떤
// 메세지도 출력하지 않는다.
//
// 1.  Tokenizing : 스트링을 토큰들로 나눈다.
// 2.  Parsing    : 토큰들로 식을 파싱한다. 이 때 내부적으로 infix인 수식을
//                  postfix로 변환한다.
// 3.  Evaluation : Stack을 써서 postfix expression을 실행한다.
//
// 위의 세 단계를 거쳐서, 입력이 하나 주어지면 아래의 두 결과를 출력한다.
//
// - Infix Expression을 Postfix Expression으로 변환한 결과
// - 그리고 그 수식을 계산한 결과
//
// 1. Tokenizing
// --------
// 주어진 문자열을 토큰들로 나눈다. 사용되는 글자는 모두 아래와 같다.
// 아래의 목록에 있지 않은 글자가 주어진경우, 에러를 낸다.
//
// `0`, `1`, `2`, `3`, `4`, `5`, `6`, `7`, `8`, `9`,
// `+`, `-`, `*`, `/`, `%`, `^`, `(`, `)`, <code> </code><sup>(space)</sup>, `\t`
//
// 토크나이징은 스페이스와 탭, 두 공백문자를 써서 이뤄지며, 이 공백문자들은
// 파서에게 전달되지 않는다. 그리고 `0`...`9`에 해당하는 숫자들의 경우,
// 공백문자로 구분되지 않았을 경우 하나의 토큰으로 붙어서 파서에게 전달된다.
// 숫자의 규칙은 아래와 같다.
//
//      <digit> ::= "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9"
//     <number> ::= <digit> | <digit><number>
//
// Note: FAQ를 보면 입력이 0으로 시작하지 않는다고 주어지긴 했지만, 스펙에
// leading-zero는 에러로 처리해야한다는 규정이 있는것도 아니고, leading-zero가
// 있다고해서 계산에 문제가 생기는것도 아니어서 받아들이기로 하였습니다.
//
// 2. Parsing
// --------
// 아래의 규칙에 맞춰서 재귀하향파서를 구현하였다.
//
//              <expr> ::= <term> | <term> <add-op> <expr>
//              <term> ::= <signed-factor> | <signed-factor> <mult-op> <term>
//     <signed-factor> ::= <factor> | "-" <signed-factor>
//            <factor> ::= <element> | <element> "^" <factor>
//           <element> ::= "(" <expr> ")" | <number>
//
//            <add-op> ::= "+" | "-"
//           <mult-op> ::= "*" | "/" | "%"
//
// 괄호의 짝이 맞지 않는 등의 틀린 문법으로 인해 파싱이 불가능할경우 에러를 낸다.
//
// 파서는 위의 규칙대로 Infix Expression을 파싱하여, Postfix Expression으로
// 변환하여 Evaluator에게 넘겨준다. 이때 Postfix Expression의 문법은 아래와 같다.
//
//       <postfix> ::= <number>
//                   | <postfix> " " <postfix> " " <binary-op>
//                   | <postfix> " " <unary-op>
//
//     <binary-op> ::= "+" | "-" | "*" | "/" | "%" | "^"
//      <unary-op> ::= "~"
//
// Infix Expression에선 `-`가 쓰이는 위치에 따라 binary operator일수도 있고, unary
// operator일수도 있었지만, Postfix Expression에서의 `-`는 무조건 binary
// operator이고, unary operator는 `~`로 표시된다.
//
// 3. Evaluation
// --------
// Postfix Expression을 받아, 스택을 사용하여 실행한다. 구현방법은 자명하므로
// 생략한다.
//
// 이때 Divide by zero 등의 이유로 실행이 불가능한 수식이 Evaluator에게 전달되었을
// 경우, 에러를 낸다. 실행이 불가능한경우는 아래 세가지이다.
//
// - `n 0 /`
// - `n 0 %`
// - `0 <음수> ^`
//
// 여기서 특기할만한 점은, 스펙상 `0 0 ^`을 `1`로 처리해야한다는 점이다.
//
// Note: Evaluation 단계에서 에러가 발생한경우, 실행이 불가능한것이지 파싱은
// 가능하므로 postfix expression을 출력한 뒤 ERROR를 발생한다고 생각할 수 있지만,
// 그래선 안된다. FAQ 참고.

import java.io.*;
import java.util.*;


//
// 프로그램 진입점
//
public class CalculatorTest {
    public static void main(final String args[]) {
        final BufferedReader br = new BufferedReader(new InputStreamReader(System.in));

        while (true) {
            try {
                String input = br.readLine();
                if (input == null || input.compareTo("q") == 0) { return; }

                System.out.println(eval(input).orElse("ERROR"));
            } catch (Exception e) {
                System.err.println("ERROR");

                // 과제 제출용으로 위와같이 에러를 숨겼음.
                // System.err.println("입력이 잘못되었습니다. 오류 : " + e.toString());
            }
        }
    }

    // 토크나이징에 사용될 렉서
    private static final Lexer<Type> lexer = new Lexer<Type>() {{
        add("[0-9]+",           Type.Number);
        add("[+\\-*\\/%^()]",   Type.Operator);
        add("[ \t]",            Type.Whitespace);
    }};

    // Infix expression이 담긴 문자열을 주면, 실행 결과를 반환하거나 실패사실을
    // 반환함. Exception-safe 함.
    private static Option<String> eval(final String input) {
        return lexer.lex(input)
            .map(new Function<List<Token<Type>>, List<Token<Type>>>() {
                @Override
                public List<Token<Type>> apply(List<Token<Type>> t) {
                    for (Iterator<Token<Type>> iter = t.listIterator(); iter.hasNext(); ) {
                        Token<Type> token = iter.next();
                        if (token.kind == Type.Whitespace) { iter.remove(); }
                    }

                    return t;
                }
            })
            .flatMap(Parser.parse)
            .flatMap(Calc.calc)
            .map(new Function<CalcResult, String>() {
                @Override
                public String apply(CalcResult t) {
                    StringBuilder buf = new StringBuilder();

                    Iterator<Token<Type>> iter = t.postfix.listIterator();
                    if (iter.hasNext()) {
                        buf.append(iter.next().sequence);

                        while (iter.hasNext()) {
                            buf.append(' ');
                            buf.append(iter.next().sequence);
                        }
                    }

                    buf.append('\n');
                    buf.append(t.result);
                    return buf.toString();
                }
            });
    }
}

// 렉서에 토큰별 구분자로 쓰이는 열거형
enum Type { Number, Operator, Whitespace }

// Monadic composition으로 구현한 재귀하향파서
class Parser {
    public static Function<List<Token<Type>>, Option<List<Token<Type>>>> parse = new Function<List<Token<Type>>, Option<List<Token<Type>>>>() {
        @Override public Option<List<Token<Type>>> apply(List<Token<Type>> t) { return _parse(t); }
    };
    private static Option<List<Token<Type>>> _parse(List<Token<Type>> tokens) {
        Parser p = new Parser(tokens);
        Context c0 = new Context();

        return p.tryExpr.apply(c0)
            .flatMap(new Function<Context, Option<List<Token<Type>>>>() {
                @Override
                public Option<List<Token<Type>>> apply(Context c) {
                    if (c.cursor != tokens.size()) { return Option.empty(); }

                    return Option.of(c.terminate().output);
                }
            });
    }

    // Internal class constructor
    private final List<Token<Type>> tokens;
    private Parser(List<Token<Type>> tokens) { this.tokens = tokens; }

    // Alternatives for `tokens.get(idx)` function which is exception-safe
    private Option<Token<Type>> tokenAt(int index) {
        return 0 <= index && index < tokens.size()
            ? Option.of(tokens.get(index))
            : Option.empty();
    }

    // Missing `or` function for Option<T>
    private static <T> Option<T> or(Option<T> left, Option<T> right) {
        return left.isPresent() ? left
            : right.isPresent() ? right
            : Option.empty();
    }

    // Parser context. 파서가 파싱중 사용하는 컨텍스트 클래스이다.
    // Immutable이다.
    //
    // Reference: Shunting-yard Algorithm
    private static class Context {
        public final int cursor;
        private final ArrayList<Token<Type>> output;
        private final ArrayList<Token<Type>> op_stack;

        private Context(int cursor,
                ArrayList<Token<Type>> output,
                ArrayList<Token<Type>> op_stack)
        {
            this.cursor = cursor;
            this.output = output;
            this.op_stack = op_stack;
        }

        // 빈 컨텍스트 객체를 생성한다.
        public Context() {
            cursor = 0;
            output = new ArrayList<Token<Type>>();
            op_stack = new ArrayList<Token<Type>>();
        }

        // Shunting-yard Algorithm에 맞춰, 토큰들의 순서를 Infix에서 Postfix로
        // 재배열한다.
        public Function<Token<Type>, Context> push = new Function<Token<Type>, Context>() {
            @Override
            public Context apply(Token<Type> t) { return _push(t); }
        };
        private Context _push(Token<Type> t) {
            switch (t.kind) {
            case Number: {
                final ArrayList<Token<Type>> output = new ArrayList<Token<Type>>(this.output) {{
                    add(t);
                }};

                return new Context(cursor + 1, output, op_stack); }
            case Operator: {
                final ArrayList<Token<Type>>
                    output = new ArrayList<Token<Type>>(this.output),
                    op_stack = new ArrayList<Token<Type>>(this.op_stack);

                switch (t.sequence) {
                case "(":
                    op_stack.add(t);
                    break;
                case ")":
                    while (true) {
                        if (op_stack.isEmpty()) { throw new IllegalArgumentException(); }

                        Token<Type> pop = op_stack.remove(op_stack.size() - 1);
                        if (pop.sequence.equals("(")) { break; }

                        output.add(pop);
                    }
                    break;
                default:
                    while (!op_stack.isEmpty()) {
                        Token<Type> top = op_stack.get(op_stack.size() - 1);
                        boolean condition = !top.sequence.equals("(") &&
                            ( is_left_assoc(t)
                            ? precedence(t) <= precedence(top)
                            : precedence(t) < precedence(top) );

                        if (!condition) { break; }

                        output.add(top);
                        op_stack.remove(op_stack.size() - 1);
                    }

                    op_stack.add(t);
                }

                return new Context(cursor + 1, output, op_stack); }
            default:
                return this;
            }
        }

        // 파서 컨텍스트에 파싱이 끝났음을 알린다. op_stack에 들어있는 잔여
        // 토큰들을 모두 output 스택으로 옮긴다.
        public Context terminate() {
            final ArrayList<Token<Type>>
                output = new ArrayList<Token<Type>>(this.output),
                op_stack = new ArrayList<Token<Type>>(this.op_stack);

            while (!op_stack.isEmpty()) {
                Token<Type> t = op_stack.remove(op_stack.size() - 1);
                output.add(t);
            }

            return new Context(cursor, output, op_stack);
        }

        @Override
        public String toString() {
            return "Content(cursor: "+cursor+", output: "+output+", op_stack: "+op_stack+")";
        }

        private static int precedence(Token<Type> op) {
            switch (op.sequence) {
            case "^":                       return 4;
            case "~":                       return 3;
            case "*": case "/": case "%":   return 2;
            case "+": case "-":             return 1;
            default: throw new IllegalArgumentException();
            }
        }

        private static boolean is_left_assoc(Token<Type> op) {
            switch (op.sequence) {
            case "*": case "/": case "%":
            case "+": case "-":             return true;
            case "^": case "~":             return false;
            default: throw new IllegalArgumentException();
            }
        }
    }

    // Recursive descent parser
    //
    //          <expr> ::= <term> | <term> <add-op> <expr>
    //          <term> ::= <signed-factor> | <signed-factor> <mult-op> <term>
    // <signed-factor> ::= <factor> | "-" <signed-factor>
    //        <factor> ::= <element> | <element> "^" <factor>
    //       <element> ::= "(" <expr> ")" | <number>
    //
    //        <add-op> ::= "+" | "-"
    //       <mult-op> ::= "*" | "/" | "%"
    private Option<Context> _tryExpr(Context ctxt) {
        return or(
            Option.of(ctxt)
                .flatMap(this.tryTerm)
                .flatMap(this.tryOp("+", "-"))
                .flatMap(this.tryExpr),
            Option.of(ctxt)
                .flatMap(this.tryTerm)
        );
    }
    private Option<Context> _tryTerm(Context ctxt) {
        return or(
            Option.of(ctxt)
                .flatMap(this.trySignedFactor)
                .flatMap(this.tryOp("*", "/", "%"))
                .flatMap(this.tryTerm),
            Option.of(ctxt)
                .flatMap(this.trySignedFactor)
        );
    }
    private Option<Context> _trySignedFactor(Context ctxt) {
        return or(
            Option.of(ctxt)
                .flatMap(this.tryUnaryMinus)
                .flatMap(this.trySignedFactor),
            Option.of(ctxt)
                .flatMap(this.tryFactor)
        );
    }
    private Option<Context> _tryUnaryMinus(Context ctxt) {
        return tokenAt(ctxt.cursor)
            .flatMap(new Function<Token<Type>, Option<Token<Type>>>() {
                @Override
                public Option<Token<Type>> apply(Token<Type> t) {
                    if (!(t.kind == Type.Operator && t.sequence.equals("-"))) { return Option.empty(); }

                    return Option.of(new Token<Type>("~", Type.Operator));
                }
            })
            .map(ctxt.push);
    }
    private Option<Context> _tryFactor(Context ctxt) {
        return or(
            Option.of(ctxt)
                .flatMap(this.tryElement)
                .flatMap(this.tryOp("^"))
                .flatMap(this.tryFactor),
            Option.of(ctxt)
                .flatMap(this.tryElement)
        );
    }
    private HashMap<Context, Option<Context>> cache = new HashMap<Context, Option<Context>>();
    private Option<Context> _tryElement(Context ctxt) {
        final Parser self = this;
        java.util.function.Function<Context, Option<Context>> compute = new java.util.function.Function<Context, Option<Context>>() {
            @Override
            public Option<Context> apply(Context c) {
                return or(
                    Option.of(c)
                        .flatMap(self.tryOp("("))
                        .flatMap(self.tryExpr)
                        .flatMap(self.tryOp(")")),
                    Option.of(c)
                        .flatMap(self.tryNumber)
                );
            }
        };

        // Memoization
        return cache.computeIfAbsent(ctxt, compute);
    }
    private Function<Context, Option<Context>> tryOp(String... ops) {
        return new Function<Context, Option<Context>>() {
            @Override
            public Option<Context> apply(Context ctxt) {
                return tokenAt(ctxt.cursor)
                    .filter(new Predicate<Token<Type>>() {
                        @Override
                        public boolean test(Token<Type> t) {
                            return t.kind == Type.Operator
                                && Arrays.asList(ops).contains(t.sequence);
                        }
                    })
                    .map(ctxt.push);
            }
        };
    }
    private Option<Context> _tryNumber(Context ctxt) {
        return tokenAt(ctxt.cursor)
            .filter(new Predicate<Token<Type>>() {
                @Override
                public boolean test(Token<Type> t) {
                    return t.kind == Type.Number;
                }
            })
            .map(ctxt.push);
    }

    private final Function<Context, Option<Context>>
        tryExpr         = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _tryExpr        (c); } },
        tryTerm         = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _tryTerm        (c); } },
        trySignedFactor = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _trySignedFactor(c); } },
        tryUnaryMinus   = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _tryUnaryMinus  (c); } },
        tryFactor       = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _tryFactor      (c); } },
        tryElement      = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _tryElement     (c); } },
        tryNumber       = new Function<Context, Option<Context>>() { @Override public Option<Context> apply(Context c) { return _tryNumber      (c); } };
}


//
// Postfix Expression 계산기
//
class Calc {
    // 주어진 postfix expression을 계산하여 Option.of(CalcResult)로 그 결과를 반환한다.
    // Devide by 0와 같이 에러가 있었을경우 Option.empty() 를 반환한다.
    public static Function<List<Token<Type>>, Option<CalcResult>> calc = new Function<List<Token<Type>>, Option<CalcResult>>() {
        @Override public Option<CalcResult> apply(List<Token<Type>> p) { return _calc(p); }
    };
    private static Option<CalcResult> _calc(List<Token<Type>> postfix) {
        Stack<Long> operands = new Stack<Long>();

        for (Token<Type> token: postfix) {
            switch (token.kind) {
            case Number:
                operands.push(Long.parseLong(token.sequence));
                break;
            case Operator:
                Long result;

                if (token.sequence.equals("~")) {
                    // Unary operator
                    Long operand = operands.pop();

                    result = -operand;
                } else {
                    // Binary operator
                    Long right = operands.pop();
                    Long left = operands.pop();

                    switch (token.sequence) {
                    case "+": result = left + right; break;
                    case "-": result = left - right; break;
                    case "*": result = left * right; break;
                    case "/":
                        if (right == 0) { return Option.empty(); }
                        result = left / right;
                        break;
                    case "%":
                        if (right == 0) { return Option.empty(); }
                        result = left % right;
                        break;
                    case "^":
                        if (left == 0 && right < 0) { return Option.empty(); }
                        result = (long) Math.pow(left, right);
                        break;
                    default: throw new IllegalArgumentException();
                    }
                }

                operands.push(result);
                break;
            }
        }

        return Option.of(new CalcResult(postfix, operands.pop()));
    }
}

//
// Calc#calc 메서드의 수행 결과를 담고있는 클래스.
//
class CalcResult {
    public final List<Token<Type>> postfix;
    public final long result;

    public CalcResult(List<Token<Type>> postfix, long result) {
        this.postfix = postfix;
        this.result = result;
    }
}
